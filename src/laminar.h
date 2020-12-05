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
#ifndef LAMINAR_LAMINAR_H_
#define LAMINAR_LAMINAR_H_

#include "run.h"
#include "monitorscope.h"
#include "context.h"
#include "database.h"

#include <unordered_map>
#include <kj/filesystem.h>
#include <kj/async-io.h>

// Context name to context object map
typedef std::unordered_map<std::string, std::shared_ptr<Context>> ContextMap;

class Server;
class Json;

class Http;
class Rpc;

struct Settings {
    const char* home;
    const char* bind_rpc;
    const char* bind_http;
    const char* archive_url;
};

// The main class implementing the application's business logic.
class Laminar final {
public:
    Laminar(Server& server, Settings settings);
    ~Laminar() noexcept;

    // Queues a job, returns immediately. Return value will be nullptr if
    // the supplied name is not a known job.
    std::shared_ptr<Run> queueJob(std::string name, ParamMap params = ParamMap());

    // Return the latest known number of the named job
    uint latestRun(std::string job);

    // Given a job name and number, return existence and (via reference params)
    // its current log output and whether the job is ongoing
    bool handleLogRequest(std::string name, uint num, std::string& output, bool& complete);

    // Given a relevant scope, returns a JSON string describing the current
    // server status. Content differs depending on the page viewed by the user,
    // which should be provided as part of the scope.
    std::string getStatus(MonitorScope scope);

    // Implements the laminarc function of setting arbitrary parameters on a run,
    // (typically the current run) which will be made available in the environment
    // of subsequent scripts.
    bool setParam(std::string job, uint buildNum, std::string param, std::string value);

    // Gets the list of jobs currently waiting in the execution queue
    const std::list<std::shared_ptr<Run>>& listQueuedJobs();

    // Gets the list of currently executing jobs
    const RunSet& listRunningJobs();

    // Gets the list of known jobs - scans cfg/jobs for *.run files
    std::list<std::string> listKnownJobs();

    // Fetches the content of an artifact given its filename relative to
    // $LAMINAR_HOME/archive. Ideally, this would instead be served by a
    // proper web server which handles this url.
    kj::Maybe<kj::Own<const kj::ReadableFile>> getArtefact(std::string path);

    // Given the name of a job, populate the provided string reference with
    // SVG content describing the last known state of the job. Returns false
    // if the job is unknown.
    bool handleBadgeRequest(std::string job, std::string& badge);

    // Aborts a single job
    bool abort(std::string job, uint buildNum);

    // Abort all running jobs
    void abortAll();

private:
    bool loadConfiguration();
    void loadCustomizations();
    void assignNewJobs();
    bool canQueue(const Context& ctx, const Run& run) const;
    bool tryStartRun(std::shared_ptr<Run> run, int queueIndex);
    void handleRunFinished(Run*);
    // expects that Json has started an array
    void populateArtifacts(Json& out, std::string job, uint num) const;

    Run* activeRun(const std::string name, uint num) {
        auto it = activeJobs.byNameNumber().find(boost::make_tuple(name, num));
        return it == activeJobs.byNameNumber().end() ? nullptr : it->get();
    }

    std::list<std::shared_ptr<Run>> queuedJobs;

    std::unordered_map<std::string, uint> buildNums;

    std::unordered_map<std::string, std::set<std::string>> jobContexts;

    std::unordered_map<std::string, std::string> jobDescriptions;

    std::unordered_map<std::string, std::string> jobGroups;

    RunSet activeJobs;
    Database* db;
    Server& srv;
    ContextMap contexts;
    kj::Path homePath;
    kj::Own<const kj::Directory> fsHome;
    uint numKeepRunDirs;
    std::string archiveUrl;

    kj::Own<Http> http;
    kj::Own<Rpc> rpc;
};

#endif // LAMINAR_LAMINAR_H_
