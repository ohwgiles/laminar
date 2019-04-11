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
#include "run.h"
#include "node.h"
#include "conf.h"
#include "log.h"

#include <iostream>
#include <unistd.h>
#include <signal.h>

// short syntax helper for kj::Path
template<typename T>
inline kj::Path operator/(const kj::Path& p, const T& ext) {
    return p.append(ext);
}

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


Run::Run(std::string name, ParamMap pm, kj::Path&& rootPath) :
    result(RunState::SUCCESS),
    lastResult(RunState::UNKNOWN),
    name(name),
    params(kj::mv(pm)),
    queuedAt(time(nullptr)),
    rootPath(kj::mv(rootPath)),
    started(kj::newPromiseAndFulfiller<void>())
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

bool Run::configure(uint buildNum, std::shared_ptr<Node> nd, const kj::Directory& fsHome)
{
    kj::Path cfgDir{"cfg"};

    // create the run directory
    kj::Path rd{"run",name,std::to_string(buildNum)};
    bool createWorkdir = true;
    KJ_IF_MAYBE(ls, fsHome.tryLstat(rd)) {
        LASSERT(ls->type == kj::FsNode::Type::DIRECTORY);
        LLOG(WARNING, "Working directory already exists, removing", rd.toString());
        if(fsHome.tryRemove(rd) == false) {
            LLOG(WARNING, "Failed to remove working directory");
            createWorkdir = false;
        }
    }
    if(createWorkdir && fsHome.tryOpenSubdir(rd, kj::WriteMode::CREATE|kj::WriteMode::CREATE_PARENT) == nullptr) {
        LLOG(ERROR, "Could not create working directory", rd.toString());
        return false;
    }

    // create an archive directory
    kj::Path archive = kj::Path{"archive",name,std::to_string(buildNum)};
    if(fsHome.exists(archive)) {
        LLOG(WARNING, "Archive directory already exists", archive.toString());
    } else if(fsHome.tryOpenSubdir(archive, kj::WriteMode::CREATE|kj::WriteMode::CREATE_PARENT) == nullptr) {
        LLOG(ERROR, "Could not create archive directory", archive.toString());
        return false;
    }

    // create a workspace for this job if it doesn't exist
    kj::Path ws{"run",name,"workspace"};
    if(!fsHome.exists(ws)) {
        fsHome.openSubdir(ws, kj::WriteMode::CREATE|kj::WriteMode::CREATE_PARENT);
        // prepend the workspace init script
        if(fsHome.exists(cfgDir/"jobs"/(name+".init")))
            addScript(cfgDir/"jobs"/(name+".init"), kj::mv(ws));
    }

    // add scripts
    // global before-run script
    if(fsHome.exists(cfgDir/"before"))
        addScript(cfgDir/"before", rd.clone());
    // per-node before-run script
    if(fsHome.exists(cfgDir/"nodes"/(nd->name+".before")))
        addScript(cfgDir/"nodes"/(nd->name+".before"), rd.clone());
    // job before-run script
    if(fsHome.exists(cfgDir/"jobs"/(name+".before")))
        addScript(cfgDir/"jobs"/(name+".before"), rd.clone());
    // main run script. must exist.
    addScript(cfgDir/"jobs"/(name+".run"), rd.clone());
    // job after-run script
    if(fsHome.exists(cfgDir/"jobs"/(name+".after")))
        addScript(cfgDir/"jobs"/(name+".after"), rd.clone(), true);
    // per-node after-run script
    if(fsHome.exists(cfgDir/"nodes"/(nd->name+".after")))
        addScript(cfgDir/"nodes"/(nd->name+".after"), rd.clone(), true);
    // global after-run script
    if(fsHome.exists(cfgDir/"after"))
        addScript(cfgDir/"after", rd.clone(), true);

    // add environment files
    if(fsHome.exists(cfgDir/"env"))
        addEnv(cfgDir/"env");
    if(fsHome.exists(cfgDir/"nodes"/(nd->name+".env")))
        addEnv(cfgDir/"nodes"/(nd->name+".env"));
    if(fsHome.exists(cfgDir/"jobs"/(name+".env")))
        addEnv(cfgDir/"jobs"/(name+".env"));

    // add job timeout if specified
    if(fsHome.exists(cfgDir/"jobs"/(name+".conf"))) {
        timeout = parseConfFile((rootPath/cfgDir/"jobs"/(name+".conf")).toString(true).cStr()).get<int>("TIMEOUT", 0);
    }

    // All good, we've "started"
    startedAt = time(nullptr);
    build = buildNum;
    node = nd;

    // notifies the rpc client if the start command was used
    started.fulfiller->fulfill();

    return true;
}

std::string Run::reason() const {
    return reasonMsg;
}

bool Run::step() {
    if(!scripts.size())
        return true;

    Script currentScript = kj::mv(scripts.front());
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

        std::string PATH = (rootPath/"cfg"/"scripts").toString(true).cStr();
        if(const char* p = getenv("PATH")) {
            PATH.append(":");
            PATH.append(p);
        }

        LSYSCALL(chdir((rootPath/currentScript.cwd).toString(true).cStr()));

        // conf file env vars
        for(kj::Path& file : env) {
            StringMap vars = parseConfFile((rootPath/file).toString(true).cStr());
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
        setenv("WORKSPACE", (rootPath/"run"/name/"workspace").toString(true).cStr(), true);
        setenv("ARCHIVE", (rootPath/"archive"/name/buildNum).toString(true).cStr(), true);

        fprintf(stderr, "[laminar] Executing %s\n", currentScript.path.toString().cStr());
        kj::String execPath = (rootPath/currentScript.path).toString(true);
        execl(execPath.cStr(), execPath.cStr(), NULL);
        // cannot use LLOG because stdout/stderr are captured
        fprintf(stderr, "[laminar] Failed to execute %s\n", currentScript.path.toString().cStr());
        _exit(1);
    }

    LLOG(INFO, "Forked", currentScript.path, currentScript.cwd, pid);
    close(pfd[1]);

    current_pid = pid;
    output_fd = pfd[0];
    return false;
}

void Run::addScript(kj::Path scriptPath, kj::Path scriptWorkingDir, bool runOnAbort) {
    scripts.push({kj::mv(scriptPath), kj::mv(scriptWorkingDir), runOnAbort});
}

void Run::addEnv(kj::Path path) {
    env.push_back(kj::mv(path));
}

void Run::abort(bool respectRunOnAbort) {
    while(scripts.size() && (!respectRunOnAbort || !scripts.front().runOnAbort))
        scripts.pop();
    // if the Maybe is empty, wait() was already called on this process
    KJ_IF_MAYBE(p, current_pid) {
        kill(-*p, SIGTERM);
    }
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
