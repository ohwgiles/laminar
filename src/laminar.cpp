///
/// Copyright 2015-2019 Oliver Giles
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
#include "laminar.h"
#include "server.h"
#include "conf.h"
#include "log.h"
#include "http.h"
#include "rpc.h"

#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fstream>
#include <zlib.h>

#define COMPRESS_LOG_MIN_SIZE 1024

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

// rapidjson::Writer with a StringBuffer is used a lot in Laminar for
// preparing JSON messages to send to HTTP clients. A small wrapper
// class here reduces verbosity later for this common use case.
class Json : public rapidjson::Writer<rapidjson::StringBuffer> {
public:
    Json() : rapidjson::Writer<rapidjson::StringBuffer>(buf) { StartObject(); }
    template<typename T>
    Json& set(const char* key, T value) { String(key); Int64(value); return *this; }
    Json& startObject(const char* key) { String(key); StartObject(); return *this; }
    Json& startArray(const char* key) { String(key); StartArray(); return *this; }
    const char* str() { EndObject(); return buf.GetString(); }
private:
    rapidjson::StringBuffer buf;
};
template<> Json& Json::set(const char* key, double value) { String(key); Double(value); return *this; }
template<> Json& Json::set(const char* key, const char* value) { String(key); String(value); return *this; }
template<> Json& Json::set(const char* key, std::string value) { String(key); String(value.c_str()); return *this; }

// short syntax helpers for kj::Path
template<typename T>
inline kj::Path operator/(const kj::Path& p, const T& ext) {
    return p.append(ext);
}
template<typename T>
inline kj::Path operator/(const std::string& p, const T& ext) {
    return kj::Path{p}/ext;
}

typedef std::string str;

Laminar::Laminar(Server &server, Settings settings) :
    settings(settings),
    srv(server),
    homePath(kj::Path::parse(&settings.home[1])),
    fsHome(kj::newDiskFilesystem()->getRoot().openSubdir(homePath, kj::WriteMode::MODIFY)),
    http(kj::heap<Http>(*this)),
    rpc(kj::heap<Rpc>(*this))
{
    LASSERT(settings.home[0] == '/');

    if(fsHome->exists(homePath/"cfg"/"nodes")) {
        LLOG(ERROR, "Found node configuration directory cfg/nodes. Nodes have been deprecated, please migrate to contexts. Laminar will now exit.");
        exit(EXIT_FAILURE);
    }

    archiveUrl = settings.archive_url;
    if(archiveUrl.back() != '/')
        archiveUrl.append("/");

    numKeepRunDirs = 0;

    db = new Database((homePath/"laminar.sqlite").toString(true).cStr());
    // Prepare database for first use
    // TODO: error handling
    db->exec("CREATE TABLE IF NOT EXISTS builds("
             "name TEXT, number INT UNSIGNED, node TEXT, queuedAt INT, "
             "startedAt INT, completedAt INT, result INT, output TEXT, "
             "outputLen INT, parentJob TEXT, parentBuild INT, reason TEXT, "
             "PRIMARY KEY (name, number))");
    db->exec("CREATE INDEX IF NOT EXISTS idx_completion_time ON builds("
             "completedAt DESC)");

    // retrieve the last build numbers
    db->stmt("SELECT name, MAX(number) FROM builds GROUP BY name")
    .fetch<str,uint>([this](str name, uint build){
        buildNums[name] = build;
    });

    srv.watchPaths([this]{
        LLOG(INFO, "Reloading configuration");
        loadConfiguration();
        // config change may allow stuck jobs to dequeue
        assignNewJobs();
    }).addPath((homePath/"cfg"/"contexts").toString(true).cStr())
      .addPath((homePath/"cfg"/"jobs").toString(true).cStr())
      .addPath((homePath/"cfg").toString(true).cStr()); // for groups.conf

    loadCustomizations();
    srv.watchPaths([this]{
        LLOG(INFO, "Reloading customizations");
        loadCustomizations();
    }).addPath((homePath/"custom").toString(true).cStr());

    srv.listenRpc(*rpc, settings.bind_rpc);
    srv.listenHttp(*http, settings.bind_http);

    // Load configuration, may be called again in response to an inotify event
    // that the configuration files have been modified
    loadConfiguration();
}

void Laminar::loadCustomizations() {
    KJ_IF_MAYBE(templ, fsHome->tryOpenFile(kj::Path{"custom","index.html"})) {
        http->setHtmlTemplate((*templ)->readAllText().cStr());
    } else {
        http->setHtmlTemplate();
    }
}

uint Laminar::latestRun(std::string job) {
    auto it = activeJobs.byJobName().equal_range(job);
    if(it.first == it.second) {
        uint result = 0;
        db->stmt("SELECT MAX(number) FROM builds WHERE name = ?")
                .bind(job)
                .fetch<uint>([&](uint x){
            result = x;
        });
        return result;
    } else {
        return (*--it.second)->build;
    }
}

bool Laminar::handleLogRequest(std::string name, uint num, std::string& output, bool& complete) {
    if(Run* run = activeRun(name, num)) {
        output = run->log;
        complete = false;
        return true;
    } else { // it must be finished, fetch it from the database
        db->stmt("SELECT output, outputLen FROM builds WHERE name = ? AND number = ?")
                .bind(name, num)
                .fetch<str,int>([&](str maybeZipped, unsigned long sz) {
            str log(sz,'\0');
            if(sz >= COMPRESS_LOG_MIN_SIZE) {
                int res = ::uncompress((uint8_t*) log.data(), &sz,
                                       (const uint8_t*) maybeZipped.data(), maybeZipped.size());
                if(res == Z_OK)
                    std::swap(output, log);
                else
                    LLOG(ERROR, "Failed to uncompress log", res);
            } else {
                std::swap(output, maybeZipped);
            }
        });
        if(output.size()) {
            complete = true;
            return true;
        }
    }
    return false;
}

bool Laminar::setParam(std::string job, uint buildNum, std::string param, std::string value) {
    if(Run* run = activeRun(job, buildNum)) {
        run->params[param] = value;
        return true;
    }
    return false;
}

const std::list<std::shared_ptr<Run>>& Laminar::listQueuedJobs() {
    return queuedJobs;
}

const RunSet& Laminar::listRunningJobs() {
    return activeJobs;
}

std::list<std::string> Laminar::listKnownJobs() {
    std::list<std::string> res;
    KJ_IF_MAYBE(dir, fsHome->tryOpenSubdir(kj::Path{"cfg","jobs"})) {
        for(kj::Directory::Entry& entry : (*dir)->listEntries()) {
            if(entry.name.endsWith(".run")) {
                res.emplace_back(entry.name.cStr(), entry.name.findLast('.').orDefault(0));
            }
        }
    }
    return res;
}

void Laminar::populateArtifacts(Json &j, std::string job, uint num) const {
    kj::Path runArchive{job,std::to_string(num)};
    KJ_IF_MAYBE(dir, fsHome->tryOpenSubdir("archive"/runArchive)) {
        for(kj::StringPtr file : (*dir)->listNames()) {
            kj::FsNode::Metadata meta = (*dir)->lstat(kj::Path{file});
            if(meta.type != kj::FsNode::Type::FILE)
                continue;
            j.StartObject();
            j.set("url", archiveUrl + (runArchive/file).toString().cStr());
            j.set("filename", file.cStr());
            j.set("size", meta.size);
            j.EndObject();
        }
    }
}

std::string Laminar::getStatus(MonitorScope scope) {
    Json j;
    j.set("type", "status");
    j.set("title", getenv("LAMINAR_TITLE") ?: "Laminar");
    j.set("time", time(nullptr));
    j.startObject("data");
    if(scope.type == MonitorScope::RUN) {
        db->stmt("SELECT queuedAt,startedAt,completedAt,result,reason,parentJob,parentBuild FROM builds WHERE name = ? AND number = ?")
        .bind(scope.job, scope.num)
        .fetch<time_t, time_t, time_t, int, std::string, std::string, uint>([&](time_t queued, time_t started, time_t completed, int result, std::string reason, std::string parentJob, uint parentBuild) {
            j.set("queued", started-queued);
            j.set("started", started);
            j.set("completed", completed);
            j.set("result", to_string(RunState(result)));
            j.set("reason", reason);
            j.startObject("upstream").set("name", parentJob).set("num", parentBuild).EndObject(2);
        });
        if(const Run* run = activeRun(scope.job, scope.num)) {
            j.set("queued", run->startedAt - run->queuedAt);
            j.set("started", run->startedAt);
            j.set("result", to_string(RunState::RUNNING));
            j.set("reason", run->reason());
            j.startObject("upstream").set("name", run->parentName).set("num", run->parentBuild).EndObject(2);
            db->stmt("SELECT completedAt - startedAt FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 1")
             .bind(run->name)
             .fetch<uint>([&](uint lastRuntime){
                j.set("etc", run->startedAt + lastRuntime);
            });
        }
        j.set("latestNum", int(buildNums[scope.job]));
        j.startArray("artifacts");
        populateArtifacts(j, scope.job, scope.num);
        j.EndArray();
    } else if(scope.type == MonitorScope::JOB) {
        const uint runsPerPage = 10;
        j.startArray("recent");
        // ORDER BY param cannot be bound
        std::string order_by;
        std::string direction = scope.order_desc ? "DESC" : "ASC";
        if(scope.field == "number")
            order_by = "number " + direction;
        else if(scope.field == "result")
            order_by = "result " + direction + ", number DESC";
        else if(scope.field == "started")
            order_by = "startedAt " + direction + ", number DESC";
        else if(scope.field == "duration")
            order_by = "(completedAt-startedAt) " + direction + ", number DESC";
        else
            order_by = "number DESC";
        std::string stmt = "SELECT number,startedAt,completedAt,result,reason FROM builds WHERE name = ? ORDER BY "
                + order_by + " LIMIT ?,?";
        db->stmt(stmt.c_str())
        .bind(scope.job, scope.page * runsPerPage, runsPerPage)
        .fetch<uint,time_t,time_t,int,str>([&](uint build,time_t started,time_t completed,int result,str reason){
            j.StartObject();
            j.set("number", build)
             .set("completed", completed)
             .set("started", started)
             .set("result", to_string(RunState(result)))
             .set("reason", reason)
             .EndObject();
        });
        j.EndArray();
        db->stmt("SELECT COUNT(*),AVG(completedAt-startedAt) FROM builds WHERE name = ?")
        .bind(scope.job)
        .fetch<uint,uint>([&](uint nRuns, uint averageRuntime){
            j.set("averageRuntime", averageRuntime);
            j.set("pages", (nRuns-1) / runsPerPage + 1);
            j.startObject("sort");
            j.set("page", scope.page)
             .set("field", scope.field)
             .set("order", scope.order_desc ? "dsc" : "asc")
             .EndObject();
        });
        j.startArray("running");
        auto p = activeJobs.byJobName().equal_range(scope.job);
        for(auto it = p.first; it != p.second; ++it) {
            const std::shared_ptr<Run> run = *it;
            j.StartObject();
            j.set("number", run->build);
            j.set("context", run->context->name);
            j.set("started", run->startedAt);
            j.set("result", to_string(RunState::RUNNING));
            j.set("reason", run->reason());
            j.EndObject();
        }
        j.EndArray();
        int nQueued = 0;
        for(const auto& run : queuedJobs) {
            if (run->name == scope.job) {
                nQueued++;
            }
        }
        j.set("nQueued", nQueued);
        db->stmt("SELECT number,startedAt FROM builds WHERE name = ? AND result = ? ORDER BY completedAt DESC LIMIT 1")
        .bind(scope.job, int(RunState::SUCCESS))
        .fetch<int,time_t>([&](int build, time_t started){
            j.startObject("lastSuccess");
            j.set("number", build).set("started", started);
            j.EndObject();
        });
        db->stmt("SELECT number,startedAt FROM builds WHERE name = ? AND result <> ? ORDER BY completedAt DESC LIMIT 1")
        .bind(scope.job, int(RunState::SUCCESS))
        .fetch<int,time_t>([&](int build, time_t started){
            j.startObject("lastFailed");
            j.set("number", build).set("started", started);
            j.EndObject();
        });
        auto desc = jobDescriptions.find(scope.job);
        j.set("description", desc == jobDescriptions.end() ? "" : desc->second);
    } else if(scope.type == MonitorScope::ALL) {
        j.startArray("jobs");
        db->stmt("SELECT name,number,startedAt,completedAt,result FROM builds b JOIN (SELECT name n,MAX(number) l FROM builds GROUP BY n) q ON b.name = q.n AND b.number = q.l")
        .fetch<str,uint,time_t,time_t,int>([&](str name,uint number, time_t started, time_t completed, int result){
            j.StartObject();
            j.set("name", name);
            j.set("number", number);
            j.set("result", to_string(RunState(result)));
            j.set("started", started);
            j.set("completed", completed);
            j.EndObject();
        });
        j.EndArray();
        j.startArray("running");
        for(const auto& run : activeJobs.byStartedAt()) {
            j.StartObject();
            j.set("name", run->name);
            j.set("number", run->build);
            j.set("context", run->context->name);
            j.set("started", run->startedAt);
            j.EndObject();
        }
        j.EndArray();
        j.startObject("groups");
        for(const auto& group : jobGroups)
            j.set(group.first.c_str(), group.second);
        j.EndObject();
    } else { // Home page
        j.startArray("recent");
        db->stmt("SELECT * FROM builds ORDER BY completedAt DESC LIMIT 15")
        .fetch<str,uint,str,time_t,time_t,time_t,int>([&](str name,uint build,str context,time_t,time_t started,time_t completed,int result){
            j.StartObject();
            j.set("name", name)
             .set("number", build)
             .set("context", context)
             .set("started", started)
             .set("completed", completed)
             .set("result", to_string(RunState(result)))
             .EndObject();
        });
        j.EndArray();
        j.startArray("running");
        for(const auto& run : activeJobs.byStartedAt()) {
            j.StartObject();
            j.set("name", run->name);
            j.set("number", run->build);
            j.set("context", run->context->name);
            j.set("started", run->startedAt);
            db->stmt("SELECT completedAt - startedAt FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 1")
             .bind(run->name)
             .fetch<uint>([&](uint lastRuntime){
                j.set("etc", run->startedAt + lastRuntime);
            });
            j.EndObject();
        }
        j.EndArray();
        j.startArray("queued");
        for(const auto& run : queuedJobs) {
            j.StartObject();
            j.set("name", run->name);
            j.EndObject();
        }
        j.EndArray();
        int execTotal = 0;
        int execBusy = 0;
        for(const auto& it : contexts) {
            const std::shared_ptr<Context>& context = it.second;
            execTotal += context->numExecutors;
            execBusy += context->busyExecutors;
        }
        j.set("executorsTotal", execTotal);
        j.set("executorsBusy", execBusy);
        j.startArray("buildsPerDay");
        for(int i = 6; i >= 0; --i) {
            j.StartObject();
            db->stmt("SELECT result, COUNT(*) FROM builds WHERE completedAt > ? AND completedAt < ? GROUP BY result")
                    .bind(86400*(time(nullptr)/86400 - i), 86400*(time(nullptr)/86400 - (i-1)))
                    .fetch<int,int>([&](int result, int num){
                j.set(to_string(RunState(result)).c_str(), num);
            });
            j.EndObject();
        }
        j.EndArray();
        j.startObject("buildsPerJob");
        db->stmt("SELECT name, COUNT(*) c FROM builds WHERE completedAt > ? GROUP BY name ORDER BY c DESC LIMIT 5")
                .bind(time(nullptr) - 86400)
                .fetch<str, int>([&](str job, int count){
            j.set(job.c_str(), count);
        });
        j.EndObject();
        j.startObject("timePerJob");
        db->stmt("SELECT name, AVG(completedAt-startedAt) av FROM builds WHERE completedAt > ? GROUP BY name ORDER BY av DESC LIMIT 8")
                .bind(time(nullptr) - 7 * 86400)
                .fetch<str, uint>([&](str job, uint time){
            j.set(job.c_str(), time);
        });
        j.EndObject();
        j.startArray("resultChanged");
        db->stmt("SELECT b.name,MAX(b.number) as lastSuccess,lastFailure FROM builds AS b JOIN (SELECT name,MAX(number) AS lastFailure FROM builds WHERE result<>? GROUP BY name) AS t ON t.name=b.name WHERE b.result=? GROUP BY b.name ORDER BY lastSuccess>lastFailure, lastFailure-lastSuccess DESC LIMIT 8")
                .bind(int(RunState::SUCCESS), int(RunState::SUCCESS))
                .fetch<str, uint, uint>([&](str job, uint lastSuccess, uint lastFailure){
            j.StartObject();
            j.set("name", job)
             .set("lastSuccess", lastSuccess)
             .set("lastFailure", lastFailure);
            j.EndObject();
        });
        j.EndArray();
        j.startArray("lowPassRates");
        db->stmt("SELECT name,CAST(SUM(result==?) AS FLOAT)/COUNT(*) AS passRate FROM builds GROUP BY name ORDER BY passRate ASC LIMIT 8")
                .bind(int(RunState::SUCCESS))
                .fetch<str, double>([&](str job, double passRate){
            j.StartObject();
            j.set("name", job).set("passRate", passRate);
            j.EndObject();
        });
        j.EndArray();
        j.startArray("buildTimeChanges");
        db->stmt("SELECT name,GROUP_CONCAT(number),GROUP_CONCAT(completedAt-startedAt) FROM builds WHERE number > (SELECT MAX(number)-10 FROM builds b WHERE b.name=builds.name) GROUP BY name ORDER BY (MAX(completedAt-startedAt)-MIN(completedAt-startedAt))-STDEV(completedAt-startedAt) DESC LIMIT 8")
                .fetch<str,str,str>([&](str name, str numbers, str durations){
            j.StartObject();
            j.set("name", name);
            j.startArray("numbers");
            j.RawValue(numbers.data(), numbers.length(), rapidjson::Type::kArrayType);
            j.EndArray();
            j.startArray("durations");
            j.RawValue(durations.data(), durations.length(), rapidjson::Type::kArrayType);
            j.EndArray();
            j.EndObject();
        });
        j.EndArray();

        j.startArray("buildTimeDist");
        db->stmt("WITH ba AS (SELECT name,AVG(completedAt-startedAt) a FROM builds GROUP BY name) SELECT "
                 "COUNT(CASE WHEN               a <    30 THEN 1 END),"
                 "COUNT(CASE WHEN a >=   30 AND a <    60 THEN 1 END),"
                 "COUNT(CASE WHEN a >=   60 AND a <   300 THEN 1 END),"
                 "COUNT(CASE WHEN a >=  300 AND a <   600 THEN 1 END),"
                 "COUNT(CASE WHEN a >=  600 AND a <  1200 THEN 1 END),"
                 "COUNT(CASE WHEN a >= 1200 AND a <  2400 THEN 1 END),"
                 "COUNT(CASE WHEN a >= 2400 AND a <  3600 THEN 1 END),"
                 "COUNT(CASE WHEN a >= 3600               THEN 1 END) FROM ba")
                .fetch<uint,uint,uint,uint,uint,uint,uint,uint>([&](uint c1, uint c2, uint c3, uint c4, uint c5, uint c6, uint c7, uint c8){
            j.Int(c1);
            j.Int(c2);
            j.Int(c3);
            j.Int(c4);
            j.Int(c5);
            j.Int(c6);
            j.Int(c7);
            j.Int(c8);
        });
        j.EndArray();

    }
    j.EndObject();
    return j.str();
}

Laminar::~Laminar() noexcept try {
    delete db;
} catch (std::exception& e) {
    LLOG(ERROR, e.what());
    return;
}

bool Laminar::loadConfiguration() {
    if(const char* ndirs = getenv("LAMINAR_KEEP_RUNDIRS"))
        numKeepRunDirs = static_cast<uint>(atoi(ndirs));

    std::set<std::string> knownContexts;

    KJ_IF_MAYBE(contextsDir, fsHome->tryOpenSubdir(kj::Path{"cfg","contexts"})) {
        for(kj::Directory::Entry& entry : (*contextsDir)->listEntries()) {
            if(!entry.name.endsWith(".conf"))
                continue;

            StringMap conf = parseConfFile((homePath/"cfg"/"contexts"/entry.name).toString(true).cStr());

            std::string name(entry.name.cStr(), entry.name.findLast('.').orDefault(0));
            auto existing = contexts.find(name);
            std::shared_ptr<Context> context = existing == contexts.end() ? contexts.emplace(name, std::shared_ptr<Context>(new Context)).first->second : existing->second;
            context->name = name;
            context->numExecutors = conf.get<int>("EXECUTORS", 6);

            knownContexts.insert(name);
        }
    }

    // remove any contexts whose config files disappeared.
    // if there are no known contexts, take care not to remove and re-add the default context.
    for(auto it = contexts.begin(); it != contexts.end();) {
        if((it->first == "default" && knownContexts.size() == 0) || knownContexts.find(it->first) != knownContexts.end())
            it++;
        else
            it = contexts.erase(it);
    }

    // add a default context
    if(contexts.empty()) {
        LLOG(INFO, "Creating a default context with 6 executors");
        std::shared_ptr<Context> context(new Context);
        context->name = "default";
        context->numExecutors = 6;
        contexts.emplace("default", context);
    }

    KJ_IF_MAYBE(jobsDir, fsHome->tryOpenSubdir(kj::Path{"cfg","jobs"})) {
        for(kj::Directory::Entry& entry : (*jobsDir)->listEntries()) {
            if(!entry.name.endsWith(".conf"))
                continue;
            StringMap conf = parseConfFile((homePath/"cfg"/"jobs"/entry.name).toString(true).cStr());

            std::string jobName(entry.name.cStr(), entry.name.findLast('.').orDefault(0));

            std::string ctxPtns = conf.get<std::string>("CONTEXTS");

            if(!ctxPtns.empty()) {
                std::istringstream iss(ctxPtns);
                std::set<std::string> ctxPtnList;
                std::string ctx;
                while(std::getline(iss, ctx, ','))
                    ctxPtnList.insert(ctx);
                jobContexts[jobName].swap(ctxPtnList);
            }
            std::string desc = conf.get<std::string>("DESCRIPTION");
            if(!desc.empty()) {
                jobDescriptions[jobName] = desc;
            }
        }
    }

    jobGroups.clear();
    KJ_IF_MAYBE(groupsConf, fsHome->tryOpenFile(kj::Path{"cfg","groups.conf"}))
        jobGroups = parseConfFile((homePath/"cfg"/"groups.conf").toString(true).cStr());
    if(jobGroups.empty())
        jobGroups["All Jobs"] = ".*";

    return true;
}

std::shared_ptr<Run> Laminar::queueJob(std::string name, ParamMap params) {
    if(!fsHome->exists(kj::Path{"cfg","jobs",name+".run"})) {
        LLOG(ERROR, "Non-existent job", name);
        return nullptr;
    }

    // If the job has no contexts (maybe there is no .conf file at all), add the default context
    if(jobContexts[name].empty())
        jobContexts.at(name).insert("default");

    std::shared_ptr<Run> run = std::make_shared<Run>(name, kj::mv(params), homePath.clone());
    queuedJobs.push_back(run);

    // notify clients
    Json j;
    j.set("type", "job_queued")
        .startObject("data")
        .set("name", name)
        .EndObject();
    http->notifyEvent(j.str(), name.c_str());

    assignNewJobs();
    return run;
}

bool Laminar::abort(std::string job, uint buildNum) {
    if(Run* run = activeRun(job, buildNum))
        return run->abort();
    return false;
}

void Laminar::abortAll() {
    for(std::shared_ptr<Run> run : activeJobs) {
        run->abort();
    }
}

bool Laminar::tryStartRun(std::shared_ptr<Run> run, int queueIndex) {
    for(auto& sc : contexts) {
        std::shared_ptr<Context> ctx = sc.second;

        if(ctx->canQueue(jobContexts.at(run->name))) {
            kj::Promise<RunState> onRunFinished = run->start(buildNums[run->name] + 1, ctx, *fsHome,[this](kj::Maybe<pid_t>& pid){return srv.onChildExit(pid);});

            ctx->busyExecutors++;
            // set the last known result if exists
            db->stmt("SELECT result FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 1")
             .bind(run->name)
             .fetch<int>([=](int result){
                run->lastResult = RunState(result);
            });

            kj::Promise<void> exec = srv.readDescriptor(run->output_fd, [this, run](const char*b, size_t n){
                // handle log output
                std::string s(b, n);
                run->log += s;
                http->notifyLog(run->name, run->build, s, false);
            }).then([run, p = kj::mv(onRunFinished)]() mutable {
                // wait until leader reaped
                return kj::mv(p);
            }).then([this, run](RunState){
                handleRunFinished(run.get());
            });
            if(run->timeout > 0) {
                exec = exec.attach(srv.addTimeout(run->timeout, [r=run.get()](){
                    r->abort();
                }));
            }
            srv.addTask(kj::mv(exec));
            LLOG(INFO, "Started job", run->name, run->build, ctx->name);

            // update next build number
            buildNums[run->name]++;

            // notify clients
            Json j;
            j.set("type", "job_started")
             .startObject("data")
             .set("queueIndex", queueIndex)
             .set("name", run->name)
             .set("queued", run->startedAt - run->queuedAt)
             .set("started", run->startedAt)
             .set("number", run->build)
             .set("reason", run->reason());
            db->stmt("SELECT completedAt - startedAt FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 1")
             .bind(run->name)
             .fetch<uint>([&](uint etc){
                j.set("etc", time(nullptr) + etc);
            });
            j.EndObject();
            http->notifyEvent(j.str(), run->name.c_str());
            return true;
        }
    }
    return false;
}

void Laminar::assignNewJobs() {
    auto it = queuedJobs.begin();
    while(it != queuedJobs.end()) {
        if(tryStartRun(*it, std::distance(it, queuedJobs.begin()))) {
            activeJobs.insert(*it);
            it = queuedJobs.erase(it);
        } else {
            ++it;
        }
    }
}

void Laminar::handleRunFinished(Run * r) {
    std::shared_ptr<Context> ctx = r->context;

    ctx->busyExecutors--;
    LLOG(INFO, "Run completed", r->name, to_string(r->result));
    time_t completedAt = time(nullptr);

    // compress log
    std::string maybeZipped = r->log;
    size_t logsize = r->log.length();
    if(r->log.length() >= COMPRESS_LOG_MIN_SIZE) {
        std::string zipped(r->log.size(), '\0');
        unsigned long zippedSize = zipped.size();
        if(::compress((uint8_t*) zipped.data(), &zippedSize,
            (const uint8_t*) r->log.data(), logsize) == Z_OK) {
            zipped.resize(zippedSize);
            std::swap(maybeZipped, zipped);
        }
    }

    std::string reason = r->reason();
    db->stmt("INSERT INTO builds VALUES(?,?,?,?,?,?,?,?,?,?,?,?)")
     .bind(r->name, r->build, ctx->name, r->queuedAt, r->startedAt, completedAt, int(r->result),
           maybeZipped, logsize, r->parentName, r->parentBuild, reason)
     .exec();

    // notify clients
    Json j;
    j.set("type", "job_completed")
            .startObject("data")
            .set("name", r->name)
            .set("number", r->build)
            .set("queued", r->startedAt - r->queuedAt)
            .set("completed", completedAt)
            .set("started", r->startedAt)
            .set("result", to_string(r->result))
            .set("reason", r->reason());
    j.startArray("artifacts");
    populateArtifacts(j, r->name, r->build);
    j.EndArray();
    j.EndObject();
    http->notifyEvent(j.str(), r->name);
    http->notifyLog(r->name, r->build, "", true);
    // erase reference to run from activeJobs. Since runFinished is called in a
    // lambda whose context contains a shared_ptr<Run>, the run won't be deleted
    // until the context is destroyed at the end of the lambda execution.
    activeJobs.byRunPtr().erase(r);

    // remove old run directories
    // We cannot count back the number of directories to keep from the currently
    // finishing job because there may well be older, still-running instances of
    // this job and we don't want to delete their rundirs. So instead, check
    // whether there are any more active runs of this job, and if so, count back
    // from the oldest among them. If there are none, count back from the latest
    // known build number of this job, which may not be that of the run that
    // finished here.
    auto it = activeJobs.byJobName().equal_range(r->name);
    uint oldestActive = (it.first == it.second)? buildNums[r->name] : (*it.first)->build - 1;
    for(int i = static_cast<int>(oldestActive - numKeepRunDirs); i > 0; i--) {
        kj::Path d{"run",r->name,std::to_string(i)};
        // Once the directory does not exist, it's probably not worth checking
        // any further. 99% of the time this loop should only ever have 1 iteration
        // anyway so hence this (admittedly debatable) optimization.
        if(!fsHome->exists(d))
            break;
        fsHome->remove(d);
    }

    fsHome->symlink(kj::Path{"archive", r->name, "latest"}, std::to_string(r->build), kj::WriteMode::CREATE|kj::WriteMode::MODIFY);

    // in case we freed up an executor, check the queue
    assignNewJobs();
}

kj::Maybe<kj::Own<const kj::ReadableFile>> Laminar::getArtefact(std::string path) {
    return fsHome->openFile(kj::Path("archive").append(kj::Path::parse(path)));
}

bool Laminar::handleBadgeRequest(std::string job, std::string &badge) {
    RunState rs = RunState::UNKNOWN;
    db->stmt("SELECT result FROM builds WHERE name = ? ORDER BY number DESC LIMIT 1")
            .bind(job)
            .fetch<int>([&](int result){
        rs = RunState(result);
    });
    if(rs == RunState::UNKNOWN)
        return false;

    std::string status = to_string(rs);
    // Empirical approximation of pixel width. Not particularly stable.
    const int jobNameWidth = job.size() * 7 + 10;
    const int statusWidth = status.size() * 7 + 10;
    const char* gradient1 = (rs == RunState::SUCCESS) ? "#2aff4d" : "#ff2a2a";
    const char* gradient2 = (rs == RunState::SUCCESS) ? "#24b43c" : "#b42424";
    char* svg = NULL;
    asprintf(&svg,
R"x(
<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="20">
  <clipPath id="clip">
    <rect width="%d" height="20" rx="4"/>
  </clipPath>
  <linearGradient id="job" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="#666" />
    <stop offset="1" stop-color="#333" />
  </linearGradient>
  <linearGradient id="status" x1="0" x2="0" y1="0" y2="1">
    <stop offset="0" stop-color="%s" />
    <stop offset="1" stop-color="%s" />
  </linearGradient>
  <g clip-path="url(#clip)" font-family="DejaVu Sans,Verdana,sans-serif" font-size="12" text-anchor="middle">
    <rect width="%d" height="20" fill="url(#job)"/>
    <text x="%d" y="14" fill="#fff">%s</text>
    <rect x="%d" width="%d" height="20" fill="url(#status)"/>
    <text x="%d" y="14" fill="#000">%s</text>
  </g>
</svg>)x", jobNameWidth+statusWidth, jobNameWidth+statusWidth, gradient1, gradient2, jobNameWidth, jobNameWidth/2+1, job.data(), jobNameWidth, statusWidth, jobNameWidth+statusWidth/2, status.data());
    badge = svg;
    return true;
}

// TODO: deprecate
std::string Laminar::getCustomCss() {
    KJ_IF_MAYBE(cssFile, fsHome->tryOpenFile(kj::Path{"custom","style.css"})) {
        static bool warningShown = false;
        if(!warningShown) {
            LLOG(WARNING, "Custom CSS has been deprecated and will be removed from a future release. Use a custom HTML template instead.");
            warningShown = true;
        }
        return (*cssFile)->readAllText().cStr();
    } else {
        return std::string();
    }
}
