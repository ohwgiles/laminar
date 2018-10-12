///
/// Copyright 2015-2018 Oliver Giles
///
/// This file is part of Laminar
///
/// Laminar is free software: you can redistribute it and/or modify
/// it under the terms of the GNU General Public License as published by
/// the Free Software Foundation, either version 3 of the License, or
/// (at your option) any later version.
///
/// Laminar is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with Laminar.  If not, see <http://www.gnu.org/licenses/>
///
#include "laminar.capnp.h"

#include <capnp/ez-rpc.h>
#include <kj/vector.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define EFAILED 55

template<typename T>
static int setParams(int argc, char** argv, T& request) {
    int n = 0;
    for(int i = 0; i < argc; ++i) {
        if(strchr(argv[i], '=') == NULL)
            break;
        n++;
    }

    char* job = getenv("JOB");
    char* num = getenv("RUN");
    char* reason = getenv("LAMINAR_REASON");

    auto params = request.initParams(n + (job&&num?2:0) + (reason?1:0));

    for(int i = 0; i < n; ++i) {
        char* name = argv[i];
        char* val = strchr(name, '=');
        *val++ = '\0';
        params[i].setName(name);
        params[i].setValue(val);
    }

    int argsConsumed = n;

    if(job && num) {
        params[n].setName("=parentJob");
        params[n++].setValue(job);
        params[n].setName("=parentBuild");
        params[n++].setValue(num);
    }
    if(reason) {
        params[n].setName("=reason");
        params[n].setValue(reason);
    }

    return argsConsumed;
}

static void printTriggerLink(const char* job, uint run) {
    // use a private ANSI CSI sequence to mark the JOB:NUM so the
    // frontend can recognise it and generate a hyperlink.
    printf("\033[{%s:%d\033\\\n", job, run);
}

int main(int argc, char** argv) {
    if(argc < 2) {
        fprintf(stderr, "Usage: %s <command> [parameters...]\n", argv[0]);
        return EINVAL;
    }

    int ret = 0;
    const char* address = getenv("LAMINAR_HOST") ?: getenv("LAMINAR_BIND_RPC") ?: "unix-abstract:laminar";

    capnp::EzRpcClient client(address);
    LaminarCi::Client laminar = client.getMain<LaminarCi>();

    auto& waitScope = client.getWaitScope();

    if(strcmp(argv[1], "queue") == 0) {
        if(argc < 3) {
            fprintf(stderr, "Usage %s queue <jobName>\n", argv[0]);
            return EINVAL;
        }
        kj::Vector<capnp::RemotePromise<LaminarCi::QueueResults>> promises;
        int jobNameIndex = 2;
        // make a request for each job specified on the commandline
        do {
            auto req = laminar.queueRequest();
            req.setJobName(argv[jobNameIndex]);
            int n = setParams(argc - jobNameIndex - 1, &argv[jobNameIndex + 1], req);
            promises.add(req.send());
            jobNameIndex += n + 1;
        } while(jobNameIndex < argc);
        // pend on the promises
        for(auto& p : promises) {
            if(p.wait(waitScope).getResult() != LaminarCi::MethodResult::SUCCESS) {
                fprintf(stderr, "Failed to queue job '%s'\n", argv[2]);
                return ENOENT;
            }
        }
    } else if(strcmp(argv[1], "start") == 0 || strcmp(argv[1], "trigger") == 0) {
        if(strcmp(argv[1], "trigger") == 0)
            fprintf(stderr, "Warning: 'trigger' is deprecated, use 'queue' for the old behavior\n");
        if(argc < 3) {
            fprintf(stderr, "Usage %s queue <jobName>\n", argv[0]);
            return EINVAL;
        }
        kj::Vector<capnp::RemotePromise<LaminarCi::StartResults>> promises;
        struct: public kj::TaskSet::ErrorHandler {
            void taskFailed(kj::Exception&&) override {}
        } ignoreFailed;
        kj::TaskSet ts(ignoreFailed);
        int jobNameIndex = 2;
        // make a request for each job specified on the commandline
        do {
            auto req = laminar.startRequest();
            req.setJobName(argv[jobNameIndex]);
            int n = setParams(argc - jobNameIndex - 1, &argv[jobNameIndex + 1], req);
            ts.add(req.send().then([&ret,argv,jobNameIndex](capnp::Response<LaminarCi::StartResults> resp){
                if(resp.getResult() != LaminarCi::MethodResult::SUCCESS) {
                    fprintf(stderr, "Failed to start job '%s'\n", argv[2]);
                    ret = ENOENT;
                }
                printTriggerLink(argv[jobNameIndex], resp.getBuildNum());
            }));
            jobNameIndex += n + 1;
        } while(jobNameIndex < argc);
        ts.onEmpty().wait(waitScope);
    } else if(strcmp(argv[1], "run") == 0) {
        if(argc < 3) {
            fprintf(stderr, "Usage %s run <jobName>\n", argv[0]);
            return EINVAL;
        }
        struct: public kj::TaskSet::ErrorHandler {
            void taskFailed(kj::Exception&&) override {}
        } ignoreFailed;
        kj::TaskSet ts(ignoreFailed);
        int jobNameIndex = 2;
        // make a request for each job specified on the commandline
        do {
            auto req = laminar.runRequest();
            req.setJobName(argv[jobNameIndex]);
            int n = setParams(argc - jobNameIndex - 1, &argv[jobNameIndex + 1], req);
            ts.add(req.send().then([&ret,argv,jobNameIndex](capnp::Response<LaminarCi::RunResults> resp){
                printTriggerLink(argv[jobNameIndex], resp.getBuildNum());
                if(resp.getResult() != LaminarCi::JobResult::SUCCESS) {
                    ret = EFAILED;
                }
            }));
            jobNameIndex += n + 1;
        } while(jobNameIndex < argc);
        ts.onEmpty().wait(waitScope);
    } else if(strcmp(argv[1], "set") == 0) {
        if(argc < 3) {
            fprintf(stderr, "Usage %s set param=value\n", argv[0]);
            return EINVAL;
        }
        auto req = laminar.setRequest();
        char* eq = strchr(argv[2], '=');
        char* job = getenv("JOB");
        char* num = getenv("RUN");
        if(job && num && eq) {
            char* name = argv[2];
            *eq++ = '\0';
            char* val = eq;
            req.getRun().setJob(job);
            req.getRun().setBuildNum(atoi(num));
            req.getParam().setName(name);
            req.getParam().setValue(val);
            req.send().wait(waitScope);
        } else {
            fprintf(stderr, "Missing $JOB or $RUN or param is not in the format key=value\n");
            return EINVAL;
        }
    } else if(strcmp(argv[1], "abort") == 0) {
        if(argc != 4) {
            fprintf(stderr, "Usage %s abort <jobName> <jobNumber>\n", argv[0]);
            return EINVAL;
        }
        auto req = laminar.abortRequest();
        req.getRun().setJob(argv[2]);
        req.getRun().setBuildNum(atoi(argv[3]));
        if(req.send().wait(waitScope).getResult() != LaminarCi::MethodResult::SUCCESS)
            ret = EFAILED;
    } else if(strcmp(argv[1], "show-jobs") == 0) {
        if(argc != 2) {
            fprintf(stderr, "Usage: %s show-jobs\n", argv[0]);
            return EINVAL;
        }
        for(auto it : laminar.listKnownRequest().send().wait(waitScope).getResult()) {
            printf("%s\n", it.cStr());
        }
    } else if(strcmp(argv[1], "show-queued") == 0) {
        if(argc != 2) {
            fprintf(stderr, "Usage: %s show-queued\n", argv[0]);
            return EINVAL;
        }
        for(auto it : laminar.listQueuedRequest().send().wait(waitScope).getResult()) {
            printf("%s\n", it.cStr());
        }
    } else if(strcmp(argv[1], "show-running") == 0) {
        if(argc != 2) {
            fprintf(stderr, "Usage: %s show-running\n", argv[0]);
            return EINVAL;
        }
        for(auto it : laminar.listRunningRequest().send().wait(waitScope).getResult()) {
            printf("%s:%d\n", it.getJob().cStr(), it.getBuildNum());
        }
    } else {
        fprintf(stderr, "Unknown command %s\n", argv[1]);
        return EINVAL;
    }

    return ret;
}
