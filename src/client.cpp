///
/// Copyright 2015-2017 Oliver Giles
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

    int argsConsumed = n;

    char* job = getenv("JOB");
    char* num = getenv("RUN");
    char* reason = getenv("LAMINAR_REASON");

    if(job && num) n+=2;
    else if(reason) n++;

    if(n == 0) return argsConsumed;

    auto params = request.initParams(n);

    for(int i = 0; i < argsConsumed; ++i) {
        char* name = argv[i];
        char* val = strchr(name, '=');
        *val++ = '\0';
        params[i].setName(name);
        params[i].setValue(val);
    }

    if(job && num) {
        params[argsConsumed].setName("=parentJob");
        params[argsConsumed].setValue(job);
        params[argsConsumed+1].setName("=parentBuild");
        params[argsConsumed+1].setValue(num);
    } else if(reason) {
        params[argsConsumed].setName("=reason");
        params[argsConsumed].setValue(reason);
    }

    return argsConsumed;
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

    if(strcmp(argv[1], "trigger") == 0) {
        if(argc < 3) {
            fprintf(stderr, "Usage %s trigger <jobName>\n", argv[0]);
            return EINVAL;
        }
        kj::Vector<capnp::RemotePromise<LaminarCi::TriggerResults>> promises;
        int jobNameIndex = 2;
        // make a request for each job specified on the commandline
        do {
            auto req = laminar.triggerRequest();
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
    } else if(strcmp(argv[1], "start") == 0) {
        if(argc < 3) {
            fprintf(stderr, "Usage %s start <jobName>\n", argv[0]);
            return EINVAL;
        }
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
                printf("%s:%d\n", argv[jobNameIndex], resp.getBuildNum());
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
            req.setJobName(job);
            req.setBuildNum(atoi(num));
            req.getParam().setName(name);
            req.getParam().setValue(val);
            req.send().wait(waitScope);
        } else {
            fprintf(stderr, "Missing $JOB or $RUN or param is not in the format key=value\n");
            return EINVAL;
        }
    } else if(strcmp(argv[1], "lock") == 0) {
        auto req = laminar.lockRequest();
        req.setLockName(argv[2]);
        req.send().wait(waitScope);
    } else if(strcmp(argv[1], "release") == 0) {
        auto req = laminar.releaseRequest();
        req.setLockName(argv[2]);
        req.send().wait(waitScope);
    } else {
        fprintf(stderr, "Unknown command %s\n", argv[1]);
        return EINVAL;
    }

    return ret;
}
