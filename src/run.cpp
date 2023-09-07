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
#include "run.h"
#include "context.h"
#include "conf.h"
#include "log.h"

#include <sys/wait.h>
#include <iostream>
#include <unistd.h>
#include <signal.h>

#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/limits.h>
#endif

// short syntax helper for kj::Path
template<typename T>
inline kj::Path operator/(const kj::Path& p, const T& ext) {
    return p.append(ext);
}

std::string to_string(const RunState& rs) {
    switch(rs) {
    case RunState::QUEUED: return "queued";
    case RunState::RUNNING: return "running";
    case RunState::ABORTED: return "aborted";
    case RunState::FAILED: return "failed";
    case RunState::SUCCESS: return "success";
    default:
         return "unknown";
    }
}


Run::Run(std::string name, uint num, ParamMap pm, kj::Path&& rootPath) :
    result(RunState::SUCCESS),
    name(name),
    build(num),
    params(kj::mv(pm)),
    queuedAt(time(nullptr)),
    rootPath(kj::mv(rootPath)),
    started(kj::newPromiseAndFulfiller<void>()),
    startedFork(started.promise.fork()),
    finished(kj::newPromiseAndFulfiller<RunState>()),
    finishedFork(finished.promise.fork())
{
    for(auto it = params.begin(); it != params.end();) {
        if(it->first[0] == '=') {
            if(it->first == "=parentJob") {
                parentName = it->second;
            } else if(it->first == "=parentBuild") {
                parentBuild = atoi(it->second.c_str());
            } else if(it->first == "=reason") {
                reasonMsg = it->second;
            } else {
                LLOG(ERROR, "Unknown internal job parameter", it->first);
            }
            it = params.erase(it);
        } else
            ++it;
    }
}

Run::~Run() {
    LLOG(INFO, "Run destroyed");
}

static void setEnvFromFile(const kj::Path& rootPath, kj::Path file) {
    StringMap vars = parseConfFile((rootPath/file).toString(true).cStr());
    for(auto& it : vars) {
        setenv(it.first.c_str(), it.second.c_str(), true);
    }
}

kj::Promise<RunState> Run::start(RunState lastResult, std::shared_ptr<Context> ctx, const kj::Directory &fsHome, std::function<kj::Promise<int>(kj::Maybe<pid_t>&)> getPromise)
{
    kj::Path cfgDir{"cfg"};

    // add job timeout if specified
    if(fsHome.exists(cfgDir/"jobs"/(name+".conf"))) {
        timeout = parseConfFile((rootPath/cfgDir/"jobs"/(name+".conf")).toString(true).cStr()).get<int>("TIMEOUT", 0);
    }

    int plog[2];
    LSYSCALL(pipe(plog));

    // Fork a process leader to run all the steps of the job. This gives us a nice
    // process tree output (job name and number as the process name) and helps
    // contain any wayward descendent processes.
    pid_t leader;
    LSYSCALL(leader = fork());

    if(leader == 0) {
        // All output from this process will be captured in the plog pipe
        close(plog[0]);
        dup2(plog[1], STDOUT_FILENO);
        dup2(plog[1], STDERR_FILENO);
        close(plog[1]);

        // All initial/fixed env vars can be set here. Dynamic ones, including
        // "RESULT" and any set by `laminarc set` have to be handled in the subprocess.

        // add environment files
        if(fsHome.exists(cfgDir/"env"))
            setEnvFromFile(rootPath, cfgDir/"env");
        if(fsHome.exists(cfgDir/"contexts"/(ctx->name+".env")))
            setEnvFromFile(rootPath, cfgDir/"contexts"/(ctx->name+".env"));
        if(fsHome.exists(cfgDir/"jobs"/(name+".env")))
            setEnvFromFile(rootPath, cfgDir/"jobs"/(name+".env"));

        // parameterized vars
        for(auto& pair : params) {
            setenv(pair.first.c_str(), pair.second.c_str(), false);
        }

        std::string PATH = (rootPath/"cfg"/"scripts").toString(true).cStr();
        if(const char* p = getenv("PATH")) {
            PATH.append(":");
            PATH.append(p);
        }

        std::string runNumStr = std::to_string(build);

        setenv("PATH", PATH.c_str(), true);
        setenv("RUN", runNumStr.c_str(), true);
        setenv("JOB", name.c_str(), true);
        setenv("CONTEXT", ctx->name.c_str(), true);
        setenv("LAST_RESULT", to_string(lastResult).c_str(), true);
        setenv("WORKSPACE", (rootPath/"run"/name/"workspace").toString(true).cStr(), true);
        setenv("ARCHIVE", (rootPath/"archive"/name/runNumStr).toString(true).cStr(), true);
        // RESULT set in leader process

        // leader process assumes $LAMINAR_HOME as CWD
        LSYSCALL(chdir(rootPath.toString(true).cStr()));
        setenv("PWD", rootPath.toString(true).cStr(), 1);

        // We could just fork/wait over all the steps here directly, but then we
        // can't set a nice name for the process tree. There is pthread_setname_np,
        // but it's limited to 16 characters, which most of the time probably isn't
        // enough. Instead, we'll just exec ourselves and handle that in laminard's
        // main() by calling leader_main()
        char* procName;
        if(asprintf(&procName, "{laminar} %s:%d", name.data(), build) > 0)
#if defined(__FreeBSD__)
        {
            int  sysctl_rq[] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
            size_t self_exe_len = PATH_MAX;
            char self_exe[PATH_MAX];

            if (sysctl(sysctl_rq, 4, self_exe, &self_exe_len, NULL, 0))
                _exit(EXIT_FAILURE);

            execl(self_exe, procName, NULL); // does not return
        }
#else
            execl("/proc/self/exe", procName, NULL); // does not return
#endif
        _exit(EXIT_FAILURE);
    }

    // All good, we've "started"
    startedAt = time(nullptr);
    context = ctx;

    output_fd = plog[0];
    close(plog[1]);
    pid = leader;

    // notifies the rpc client if the start command was used
    started.fulfiller->fulfill();

    return getPromise(pid).then([this](int status){
        // The leader process passes a RunState through the return value.
        // Check it didn't die abnormally, then cast to get it back.
        result = WIFEXITED(status) ? RunState(WEXITSTATUS(status)) : RunState::ABORTED;
        finished.fulfiller->fulfill(RunState(result));
        return result;
    });
}

std::string Run::reason() const {
    return reasonMsg;
}

bool Run::abort() {
    // if the Maybe is empty, wait() was already called on this process
    KJ_IF_MAYBE(p, pid) {
        kill(-*p, SIGTERM);
        return true;
    }
    return false;
}
