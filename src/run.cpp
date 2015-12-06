///
/// Copyright 2015 Oliver Giles
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
#include "run.h"
#include "node.h"
#include "conf.h"
#include <iostream>
#include <kj/debug.h>

#include <unistd.h>

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

std::string to_string(const RunState& rs) {
    switch(rs) {
    case RunState::PENDING: return "pending";
    case RunState::RUNNING: return "running";
    case RunState::ABORTED: return "aborted";
    case RunState::FAILED: return "failed";
    case RunState::SUCCESS: return "success";
    case RunState::UNKNOWN:
    default:
         return "unknown";
    }
}


Run::Run() {
    result = RunState::SUCCESS;
    lastResult = RunState::UNKNOWN;
}

Run::~Run() {
    KJ_DBG("Run destroyed");
}

std::string Run::reason() const {
    if(!parentName.empty()) {
        return std::string("Triggered by upstream ") + parentName + " #" + std::to_string(parentBuild);
    }
    return reasonMsg;
}

bool Run::step() {
    if(!currentScript.empty() && procStatus != 0)
        result = RunState::FAILED;

    if(scripts.size()) {
        currentScript = scripts.front();
        scripts.pop();

        int pfd[2];
        pipe(pfd);
        pid_t pid = fork();
        if(pid == 0) {
            close(pfd[0]);
            dup2(pfd[1], 1);
            dup2(pfd[1], 2);
            close(pfd[1]);
            std::string buildNum = std::to_string(build);

            std::string PATH = (fs::path(laminarHome)/"cfg"/"scripts").string() + ":";
            if(const char* p = getenv("PATH")) {
                PATH.append(p);
            }

            chdir(wd.c_str());

            for(std::string file : env) {
                StringMap vars = parseConfFile(file.c_str());
                for(auto& it : vars) {
                    setenv(it.first.c_str(), it.second.c_str(), true);
                }
            }

            setenv("PATH", PATH.c_str(), true);
            setenv("lBuildNum", buildNum.c_str(), true);
            setenv("lJobName", name.c_str(), true);
            if(!node->name.empty())
                setenv("lNode", node->name.c_str(), true);
            setenv("lResult", to_string(result).c_str(), true);
            setenv("lLastResult", to_string(lastResult).c_str(), true);
            setenv("lWorkspace", (fs::path(laminarHome)/"run"/name/"workspace").string().c_str(), true);
            setenv("lArchive", (fs::path(laminarHome)/"archive"/name/buildNum.c_str()).string().c_str(), true);
            for(auto& pair : params) {
                setenv(pair.first.c_str(), pair.second.c_str(), false);
            }
            printf("[laminar] Executing %s\n", currentScript.c_str());
            execl(currentScript.c_str(), currentScript.c_str(), NULL);
            KJ_LOG(FATAL, "execl returned", strerror(errno));
            _exit(1);
        }

        KJ_LOG(INFO, "Forked", currentScript, pid);
        close(pfd[1]);
        fd = pfd[0];
        this->pid = pid;

        return false;
    } else {
        return true;
    }
}
void Run::addScript(std::string script) {
    scripts.push(script);
}
void Run::addEnv(std::string path) {
    env.push_back(path);
}
void Run::reaped(int status) {
    procStatus = status;
}

void Run::complete() {
    notifyCompletion(this);
}
