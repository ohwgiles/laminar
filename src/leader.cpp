///
/// Copyright 2019-2020 Oliver Giles
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
#include "log.h"
#include <string>
#include <unistd.h>
#include <queue>
#include <dirent.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/filesystem.h>

#include "run.h"

// short syntax helper for kj::Path
template<typename T>
inline kj::Path operator/(const kj::Path& p, const T& ext) {
    return p.append(ext);
}
template<typename T>
inline kj::Path operator/(const kj::PathPtr& p, const T& ext) {
    return p.append(ext);
}

struct Script {
    kj::Path path;
    kj::Path cwd;
    bool runOnAbort;
};

static void aggressive_recursive_kill(pid_t parent) {
    DIR* proc = opendir("/proc");
    if(!proc)
        return;

    while(struct dirent* de = readdir(proc)) {
        if(!isdigit(*de->d_name))
            continue;

        char status_file[640];
        sprintf(status_file, "/proc/%s/status", de->d_name);

        FILE* status_fp = fopen(status_file, "rb");
        if(!status_fp)
            continue;

        char status_buffer[512];
        int n = fread(status_buffer, 1, 512, status_fp);
        if(char* p = (char*)memmem(status_buffer, n, "PPid:\t", 6)) {
            pid_t ppid = strtol(p + 6, NULL, 10);
            if(ppid == parent) {
                pid_t pid = atoi(de->d_name);
                aggressive_recursive_kill(pid);
                fprintf(stderr, "[laminar] sending SIGKILL to pid %d\n", pid);
                kill(pid, SIGKILL);
            }
        }
        fclose(status_fp);
    }
    closedir(proc);
}


class Leader final : public kj::TaskSet::ErrorHandler {
public:
    Leader(kj::AsyncIoContext& ioContext, kj::Filesystem& fs, const char* jobName, uint runNumber);
    RunState run();

private:
    void taskFailed(kj::Exception&& exception) override;
    kj::Promise<void> step(std::queue<Script>& scripts);
    kj::Promise<void> reapChildProcesses();
    kj::Promise<void> readEnvPipe(kj::AsyncInputStream* stream, char* buffer);

    kj::TaskSet tasks;
    RunState result;
    kj::AsyncIoContext& ioContext;
    const kj::Directory& home;
    kj::PathPtr rootPath;
    std::string jobName;
    uint runNumber;
    pid_t currentGroupId;
    pid_t currentScriptPid;
    std::queue<Script> scripts;
    int setEnvPipe[2];
    bool aborting;
};

Leader::Leader(kj::AsyncIoContext &ioContext, kj::Filesystem &fs, const char *jobName, uint runNumber) :
    tasks(*this),
    result(RunState::SUCCESS),
    ioContext(ioContext),
    home(fs.getCurrent()),
    rootPath(fs.getCurrentPath()),
    jobName(jobName),
    runNumber(runNumber),
    aborting(false)
{
    tasks.add(ioContext.unixEventPort.onSignal(SIGTERM).then([this](siginfo_t) {
        while(scripts.size() && (!scripts.front().runOnAbort))
            scripts.pop();
        // TODO: probably shouldn't do this if we are already in a runOnAbort script
        kill(-currentGroupId, SIGTERM);
        return this->ioContext.provider->getTimer().afterDelay(2*kj::SECONDS).then([this]{
            aborting = true;
            aggressive_recursive_kill(getpid());
        });
    }));

    LSYSCALL(pipe(setEnvPipe));
    auto event = ioContext.lowLevelProvider->wrapInputFd(setEnvPipe[0], kj::LowLevelAsyncIoProvider::TAKE_OWNERSHIP);
    auto buffer = kj::heapArrayBuilder<char>(1024);
    tasks.add(readEnvPipe(event, buffer.asPtr().begin()).attach(kj::mv(event), kj::mv(buffer)));
}

RunState Leader::run()
{
    kj::Path cfgDir{"cfg"};

    // create the run directory
    kj::Path rd{"run",jobName,std::to_string(runNumber)};
    bool createWorkdir = true;
    KJ_IF_MAYBE(ls, home.tryLstat(rd)) {
        LASSERT(ls->type == kj::FsNode::Type::DIRECTORY);
        LLOG(WARNING, "Working directory already exists, removing", rd.toString());
        if(home.tryRemove(rd) == false) {
            LLOG(WARNING, "Failed to remove working directory");
            createWorkdir = false;
        }
    }
    if(createWorkdir && home.tryOpenSubdir(rd, kj::WriteMode::CREATE|kj::WriteMode::CREATE_PARENT) == nullptr) {
        LLOG(ERROR, "Could not create working directory", rd.toString());
        return RunState::FAILED;
    }

    // create an archive directory
    kj::Path archive = kj::Path{"archive",jobName,std::to_string(runNumber)};
    if(home.exists(archive)) {
        LLOG(WARNING, "Archive directory already exists", archive.toString());
    } else if(home.tryOpenSubdir(archive, kj::WriteMode::CREATE|kj::WriteMode::CREATE_PARENT) == nullptr) {
        LLOG(ERROR, "Could not create archive directory", archive.toString());
        return RunState::FAILED;
    }

    // create a workspace for this job if it doesn't exist
    kj::Path ws{"run",jobName,"workspace"};
    if(!home.exists(ws)) {
        home.openSubdir(ws, kj::WriteMode::CREATE|kj::WriteMode::CREATE_PARENT);
        // prepend the workspace init script
        if(home.exists(cfgDir/"jobs"/(jobName+".init")))
            scripts.push({cfgDir/"jobs"/(jobName+".init"), kj::mv(ws), false});
    }

    // add scripts
    // global before-run script
    if(home.exists(cfgDir/"before"))
        scripts.push({cfgDir/"before", rd.clone(), false});
    // job before-run script
    if(home.exists(cfgDir/"jobs"/(jobName+".before")))
        scripts.push({cfgDir/"jobs"/(jobName+".before"), rd.clone(), false});
    // main run script. must exist.
    scripts.push({cfgDir/"jobs"/(jobName+".run"), rd.clone(), false});
    // job after-run script
    if(home.exists(cfgDir/"jobs"/(jobName+".after")))
        scripts.push({cfgDir/"jobs"/(jobName+".after"), rd.clone(), true});
    // global after-run script
    if(home.exists(cfgDir/"after"))
        scripts.push({cfgDir/"after", rd.clone(), true});

    // Start executing scripts
    return step(scripts).then([this](){
        return result;
    }).wait(ioContext.waitScope);
}

void Leader::taskFailed(kj::Exception &&exception)
{
    LLOG(ERROR, exception);
}

kj::Promise<void> Leader::step(std::queue<Script> &scripts)
{
    if(scripts.empty())
        return kj::READY_NOW;

    Script currentScript = kj::mv(scripts.front());
    scripts.pop();

    pid_t pid = fork();
    if(pid == 0) { // child
        // unblock all signals
        sigset_t mask;
        sigfillset(&mask);
        sigprocmask(SIG_UNBLOCK, &mask, nullptr);

        // create a new process group to help us deal with any wayward forks
        setpgid(0, 0);

        std::string buildNum = std::to_string(runNumber);

        LSYSCALL(chdir(currentScript.cwd.toString(false).cStr()));

        setenv("RESULT", to_string(result).c_str(), true);

        // pass the pipe through a variable to allow laminarc to send new env back
        char pipeNum[4];
        sprintf(pipeNum, "%d", setEnvPipe[1]);
        setenv("__LAMINAR_SETENV_PIPE", pipeNum, 1);

        fprintf(stderr, "[laminar] Executing %s\n", currentScript.path.toString().cStr());
        kj::String execPath = (rootPath/currentScript.path).toString(true);

        execl(execPath.cStr(), execPath.cStr(), NULL);
        fprintf(stderr, "[laminar] Failed to execute %s\n", currentScript.path.toString().cStr());
        _exit(1);
    }

    currentScriptPid = pid;
    currentGroupId = pid;

    return reapChildProcesses().then([&](){
        return step(scripts);
    });
}

kj::Promise<void> Leader::reapChildProcesses()
{
    return ioContext.unixEventPort.onSignal(SIGCHLD).then([this](siginfo_t) -> kj::Promise<void> {
        while(true) {
            int status;
            errno = 0;
            pid_t pid = waitpid(-1, &status, WNOHANG);
            if(pid == -1 && errno == ECHILD) {
                // all children exited
                return kj::READY_NOW;
            } else if(pid == 0) {
                // child processes are still running
                if(currentScriptPid) {
                    // We could get here if a more deeply nested process was reparented to us
                    // before the primary script executed. Quietly wait until the process we're
                    // waiting for is done
                    return reapChildProcesses();
                }
                // we were aborted by the primary process already, just wait until all
                // SIGKILLs are processed
                if(aborting) {
                    return reapChildProcesses();
                }
                // Otherwise, reparented orphans are on borrowed time
                // TODO list wayward processes?
                fprintf(stderr, "[laminar] sending SIGHUP to adopted child processes\n");
                kill(-currentGroupId, SIGHUP);
                return ioContext.provider->getTimer().afterDelay(5*kj::SECONDS).then([this]{
                    // TODO: should we mark the job as failed if we had to kill reparented processes?
                    aggressive_recursive_kill(getpid());
                    return reapChildProcesses();
                }).exclusiveJoin(reapChildProcesses());
            } else if(pid == currentScriptPid) {
                // the script we were waiting for is done
                // if we already marked as failed, preserve that
                if(result == RunState::SUCCESS) {
                    if(WIFSIGNALED(status) && (WTERMSIG(status) == SIGTERM || WTERMSIG(status) == SIGKILL))
                        result = RunState::ABORTED;
                    else if(WEXITSTATUS(status) != 0)
                        result = RunState::FAILED;
                }
                currentScriptPid = 0;
            } else {
                // some reparented process was reaped
            }
        }
    });
}

kj::Promise<void> Leader::readEnvPipe(kj::AsyncInputStream *stream, char *buffer) {
    return stream->tryRead(buffer, 1, 1024).then([this,stream,buffer](size_t sz) {
        if(sz > 0) {
            buffer[sz] = '\0';
            if(char* eq = strchr(buffer, '=')) {
                *eq++ = '\0';
                setenv(buffer, eq, 1);
            }
            return readEnvPipe(stream, kj::mv(buffer));
        }
        return kj::Promise<void>(kj::READY_NOW);
    });
}

int leader_main(void) {
    auto ioContext = kj::setupAsyncIo();
    auto fs = kj::newDiskFilesystem();

    kj::UnixEventPort::captureSignal(SIGTERM);
    // Don't use captureChildExit or onChildExit because they don't provide a way to
    // reap orphaned child processes. Stick with the more fundamental onSignal.
    kj::UnixEventPort::captureSignal(SIGCHLD);

    // Becoming a subreaper means any descendent process whose parent process disappears
    // will be reparented to this one instead of init (or higher layer subreaper).
    // We do this so that the run will wait until all descedents exit before executing
    // the next step.
    prctl(PR_SET_CHILD_SUBREAPER, 1, NULL, NULL, NULL);

    // Become the leader of a new process group. This is so that all child processes
    // will also get a kill signal when the run is aborted
    setpgid(0, 0);

    // Environment inherited from main laminard process
    const char* jobName = getenv("JOB");
    std::string name(jobName);
    uint runNumber = atoi(getenv("RUN"));

    if(!jobName || !runNumber)
        return EXIT_FAILURE;

    Leader leader(ioContext, *fs, jobName, runNumber);

    // Parent process will cast back to RunState
    return int(leader.run());
}
