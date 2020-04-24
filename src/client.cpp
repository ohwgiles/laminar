///
/// Copyright 2015-2020 Oliver Giles
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
#include <unistd.h>

#define EXIT_BAD_ARGUMENT     1
#define EXIT_OPERATION_FAILED 2
#define EXIT_RUN_FAILED       3

// Definition needed for musl
typedef unsigned int uint;

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
        return EXIT_BAD_ARGUMENT;
    }

    struct: public kj::TaskSet::ErrorHandler {
        void taskFailed(kj::Exception&& e) override {
            fprintf(stderr, "%s\n", e.getDescription().cStr());
            ret = EXIT_OPERATION_FAILED;
        }
        int ret = 0;
    } errorHandler;
    kj::TaskSet ts(errorHandler);
    int& ret = errorHandler.ret;

    const char* address = getenv("LAMINAR_HOST") ?: getenv("LAMINAR_BIND_RPC") ?: "unix-abstract:laminar";

    capnp::EzRpcClient client(address);
    LaminarCi::Client laminar = client.getMain<LaminarCi>();

    auto& waitScope = client.getWaitScope();

    if(strcmp(argv[1], "queue") == 0) {
        if(argc < 3) {
            fprintf(stderr, "Usage %s queue <jobName>\n", argv[0]);
            return EXIT_BAD_ARGUMENT;
        }
        int jobNameIndex = 2;
        // make a request for each job specified on the commandline
        do {
            auto req = laminar.queueRequest();
            req.setJobName(argv[jobNameIndex]);
            int n = setParams(argc - jobNameIndex - 1, &argv[jobNameIndex + 1], req);
            ts.add(req.send().then([&ret,argv,jobNameIndex](capnp::Response<LaminarCi::QueueResults> resp){
                if(resp.getResult() != LaminarCi::MethodResult::SUCCESS) {
                    fprintf(stderr, "Failed to queue job '%s'\n", argv[jobNameIndex]);
                    ret = EXIT_OPERATION_FAILED;
                }
            }));
            jobNameIndex += n + 1;
        } while(jobNameIndex < argc);
    } else if(strcmp(argv[1], "start") == 0 || strcmp(argv[1], "trigger") == 0) {
        if(strcmp(argv[1], "trigger") == 0)
            fprintf(stderr, "Warning: 'trigger' is deprecated, use 'queue' for the old behavior\n");
        if(argc < 3) {
            fprintf(stderr, "Usage %s queue <jobName>\n", argv[0]);
            return EXIT_BAD_ARGUMENT;
        }
        kj::Vector<capnp::RemotePromise<LaminarCi::StartResults>> promises;
        int jobNameIndex = 2;
        // make a request for each job specified on the commandline
        do {
            auto req = laminar.startRequest();
            req.setJobName(argv[jobNameIndex]);
            int n = setParams(argc - jobNameIndex - 1, &argv[jobNameIndex + 1], req);
            ts.add(req.send().then([&ret,argv,jobNameIndex](capnp::Response<LaminarCi::StartResults> resp){
                if(resp.getResult() != LaminarCi::MethodResult::SUCCESS) {
                    fprintf(stderr, "Failed to start job '%s'\n", argv[2]);
                    ret = EXIT_OPERATION_FAILED;
                } else
                    printTriggerLink(argv[jobNameIndex], resp.getBuildNum());
            }));
            jobNameIndex += n + 1;
        } while(jobNameIndex < argc);
    } else if(strcmp(argv[1], "run") == 0) {
        if(argc < 3) {
            fprintf(stderr, "Usage %s run <jobName>\n", argv[0]);
            return EXIT_BAD_ARGUMENT;
        }
        int jobNameIndex = 2;
        // make a request for each job specified on the commandline
        do {
            auto req = laminar.runRequest();
            req.setJobName(argv[jobNameIndex]);
            int n = setParams(argc - jobNameIndex - 1, &argv[jobNameIndex + 1], req);
            ts.add(req.send().then([&ret,argv,jobNameIndex](capnp::Response<LaminarCi::RunResults> resp){
                if(resp.getResult() == LaminarCi::JobResult::UNKNOWN)
                    fprintf(stderr, "Failed to start job '%s'\n", argv[2]);
		else
		    printTriggerLink(argv[jobNameIndex], resp.getBuildNum());
                if(resp.getResult() != LaminarCi::JobResult::SUCCESS)
                    ret = EXIT_RUN_FAILED;
            }));
            jobNameIndex += n + 1;
        } while(jobNameIndex < argc);
    } else if(strcmp(argv[1], "set") == 0) {
        if(argc < 3) {
            fprintf(stderr, "Usage %s set param=value\n", argv[0]);
            return EXIT_BAD_ARGUMENT;
        }
        if(char* pipeNum = getenv("__LAMINAR_SETENV_PIPE")) {
            write(atoi(pipeNum), argv[2], strlen(argv[2]));
        } else {
            fprintf(stderr, "Must be run from within a laminar job\n");
            return EXIT_BAD_ARGUMENT;
        }
    } else if(strcmp(argv[1], "abort") == 0) {
        if(argc != 4) {
            fprintf(stderr, "Usage %s abort <jobName> <jobNumber>\n", argv[0]);
            return EXIT_BAD_ARGUMENT;
        }
        auto req = laminar.abortRequest();
        req.getRun().setJob(argv[2]);
        req.getRun().setBuildNum(atoi(argv[3]));
        ts.add(req.send().then([&ret](capnp::Response<LaminarCi::AbortResults> resp){
            if(resp.getResult() != LaminarCi::MethodResult::SUCCESS)
                ret = EXIT_OPERATION_FAILED;
        }));
    } else if(strcmp(argv[1], "show-jobs") == 0) {
        if(argc != 2) {
            fprintf(stderr, "Usage: %s show-jobs\n", argv[0]);
            return EXIT_BAD_ARGUMENT;
        }
        for(auto it : laminar.listKnownRequest().send().wait(waitScope).getResult()) {
            printf("%s\n", it.cStr());
        }
    } else if(strcmp(argv[1], "show-queued") == 0) {
        if(argc != 2) {
            fprintf(stderr, "Usage: %s show-queued\n", argv[0]);
            return EXIT_BAD_ARGUMENT;
        }
        for(auto it : laminar.listQueuedRequest().send().wait(waitScope).getResult()) {
            printf("%s\n", it.cStr());
        }
    } else if(strcmp(argv[1], "show-running") == 0) {
        if(argc != 2) {
            fprintf(stderr, "Usage: %s show-running\n", argv[0]);
            return EXIT_BAD_ARGUMENT;
        }
        for(auto it : laminar.listRunningRequest().send().wait(waitScope).getResult()) {
            printf("%s:%d\n", it.getJob().cStr(), it.getBuildNum());
        }
    } else {
        fprintf(stderr, "Unknown command %s\n", argv[1]);
        return EXIT_BAD_ARGUMENT;
    }

    ts.onEmpty().wait(waitScope);

    return ret;
}
