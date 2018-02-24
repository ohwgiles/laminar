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
#include "run.h"
#include "node.h"
#include "conf.h"
#include "log.h"

#include <iostream>
#include <unistd.h>
#include <signal.h>

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

std::string to_string(const RunState& rs) {
    switch(rs) {
    case RunState::PENDING: return "pending";
    case RunState::RUNNING: return "running";
    case RunState::ABORTED: return "aborted";
    case RunState::FAILED: return "failed";
    case RunState::SUCCESS: return "success";
    default:
         return "unknown";
    }
}


Run::Run() {
    result = RunState::SUCCESS;
    lastResult = RunState::UNKNOWN;
}

Run::~Run() {
    LLOG(INFO, "Run destroyed");
}

std::string Run::reason() const {
    if(!parentName.empty()) {
        return std::string("Triggered by upstream ") + parentName + " #" + std::to_string(parentBuild);
    }
    return reasonMsg;
}

bool Run::step() {
    if(scripts.size()) {
        currentScript = scripts.front();
        scripts.pop();

        int pfd[2];
        pipe(pfd);
        pid_t pid = fork();
        if(pid == 0) { // child
            // reset signal mask (SIGCHLD blocked in Laminar::start)
            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);
            sigprocmask(SIG_UNBLOCK, &mask, nullptr);

            // set pgid == pid for easy killing on abort
            setpgid(0, 0);

            close(pfd[0]);
            dup2(pfd[1], 1);
            dup2(pfd[1], 2);
            close(pfd[1]);
            std::string buildNum = std::to_string(build);

            std::string PATH = (fs::path(laminarHome)/"cfg"/"scripts").string() + ":";
            if(const char* p = getenv("PATH")) {
                PATH.append(p);
            }

            chdir(currentScript.cwd.c_str());

            // conf file env vars
            for(std::string file : env) {
                StringMap vars = parseConfFile(file.c_str());
                for(auto& it : vars) {
                    setenv(it.first.c_str(), it.second.c_str(), true);
                }
            }
            // parameterized vars
            for(auto& pair : params) {
                setenv(pair.first.c_str(), pair.second.c_str(), false);
            }

            setenv("PATH", PATH.c_str(), true);
            setenv("RUN", buildNum.c_str(), true);
            setenv("JOB", name.c_str(), true);
            if(!node->name.empty())
                setenv("NODE", node->name.c_str(), true);
            setenv("RESULT", to_string(result).c_str(), true);
            setenv("LAST_RESULT", to_string(lastResult).c_str(), true);
            setenv("WORKSPACE", (fs::path(laminarHome)/"run"/name/"workspace").string().c_str(), true);
            setenv("ARCHIVE", (fs::path(laminarHome)/"archive"/name/buildNum.c_str()).string().c_str(), true);

            fprintf(stderr, "[laminar] Executing %s\n", currentScript.path.c_str());
            execl(currentScript.path.c_str(), currentScript.path.c_str(), NULL);
            // cannot use LLOG because stdout/stderr are captured
            fprintf(stderr, "[laminar] Failed to execute %s\n", currentScript.path.c_str());
            _exit(1);
        }

        LLOG(INFO, "Forked", currentScript.path, currentScript.cwd, pid);
        close(pfd[1]);
        fd = pfd[0];
        this->pid = pid;

        return false;
    } else {
        return true;
    }
}

void Run::addScript(std::string scriptPath, std::string scriptWorkingDir) {
    scripts.push({scriptPath, scriptWorkingDir});
}

void Run::addEnv(std::string path) {
    env.push_back(path);
}

void Run::abort() {
    // clear all pending scripts
    std::queue<Script>().swap(scripts);
    kill(-pid, SIGTERM);
}

void Run::reaped(int status) {
    // once state is non-success it cannot change again
    if(result != RunState::SUCCESS)
        return;

    if(WIFSIGNALED(status) && (WTERMSIG(status) == SIGTERM || WTERMSIG(status) == SIGKILL))
        result = RunState::ABORTED;
    else if(status != 0)
        result = RunState::FAILED;
    // otherwise preserve earlier status
}

void Run::complete() {
    notifyCompletion(this);
}
