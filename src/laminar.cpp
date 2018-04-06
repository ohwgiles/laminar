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
#include "laminar.h"
#include "server.h"
#include "conf.h"
#include "log.h"

#include <sys/wait.h>
#include <fstream>
#include <zlib.h>

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

#define COMPRESS_LOG_MIN_SIZE 1024

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

// rapidjson::Writer with a StringBuffer is used a lot in Laminar for
// preparing JSON messages to send to Websocket clients. A small wrapper
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
template<> Json& Json::set(const char* key, const char* value) { String(key); String(value); return *this; }
template<> Json& Json::set(const char* key, std::string value) { String(key); String(value.c_str()); return *this; }

namespace {
// Default values when none were supplied in $LAMINAR_CONF_FILE (/etc/laminar.conf)
constexpr const char* INTADDR_RPC_DEFAULT = "unix-abstract:laminar";
constexpr const char* INTADDR_HTTP_DEFAULT = "*:8080";
constexpr const char* ARCHIVE_URL_DEFAULT = "/archive";
}

// helper for appending to boost::filesystem::path
fs::path operator+(fs::path p, const char* ext) {
    std::string leaf = p.leaf().string();
    leaf += ext;
    return p.remove_leaf()/leaf;
}

typedef std::string str;

Laminar::Laminar() {
    archiveUrl = ARCHIVE_URL_DEFAULT;
    if(char* envArchive = getenv("LAMINAR_ARCHIVE_URL"))
        archiveUrl = envArchive;
    numKeepRunDirs = 0;
    homeDir = getenv("LAMINAR_HOME") ?: "/var/lib/laminar";

    db = new Database((fs::path(homeDir)/"laminar.sqlite").string().c_str());
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

    srv = nullptr;

    // This is only a separate function because I imagined that it would
    // be nice to reload some configuration during runtime without restarting
    // the server completely. Currently not called from anywhere else
    // TODO: implement that
    loadConfiguration();
}

void Laminar::registerClient(LaminarClient* client) {
    clients.insert(client);
}

void Laminar::deregisterClient(LaminarClient* client) {
    clients.erase(client);
}

void Laminar::registerWaiter(LaminarWaiter *waiter) {
    waiters.insert(waiter);
}

void Laminar::deregisterWaiter(LaminarWaiter *waiter) {
    waiters.erase(waiter);
}

bool Laminar::setParam(std::string job, uint buildNum, std::string param, std::string value) {
    if(Run* run = activeRun(job, buildNum)) {
        run->params[param] = value;
        return true;
    }
    return false;
}


void Laminar::populateArtifacts(Json &j, std::string job, uint num) const {
    fs::path dir(fs::path(homeDir)/"archive"/job/std::to_string(num));
    if(fs::is_directory(dir)) {
        size_t prefixLen = (fs::path(homeDir)/"archive").string().length();
        size_t scopeLen = dir.string().length();
        for(fs::recursive_directory_iterator it(dir); it != fs::recursive_directory_iterator(); ++it) {
            if(!fs::is_regular_file(*it))
                continue;
            j.StartObject();
            j.set("url", archiveUrl + it->path().string().substr(prefixLen));
            j.set("filename", it->path().string().substr(scopeLen+1));
            j.EndObject();
        }
    }
}

void Laminar::sendStatus(LaminarClient* client) {
    if(client->scope.type == MonitorScope::LOG) {
        // If the requested job is currently in progress
        if(const Run* run = activeRun(client->scope.job, client->scope.num)) {
            client->sendMessage(run->log.c_str());
        } else { // it must be finished, fetch it from the database
            db->stmt("SELECT output, outputLen FROM builds WHERE name = ? AND number = ?")
              .bind(client->scope.job, client->scope.num)
              .fetch<str,int>([=](str maybeZipped, unsigned long sz) {
                str log(sz+1,'\0');
                if(sz >= COMPRESS_LOG_MIN_SIZE) {
                    int res = ::uncompress((uint8_t*) log.data(), &sz,
                         (const uint8_t*) maybeZipped.data(), maybeZipped.size());
                    if(res == Z_OK)
                        client->sendMessage(log);
                    else
                        LLOG(ERROR, "Failed to uncompress log");
                } else {
                    client->sendMessage(maybeZipped);
                }
            });
        }
        return;
    }

    Json j;
    j.set("type", "status");
    j.set("title", getenv("LAMINAR_TITLE") ?: "Laminar");
    j.set("time", time(nullptr));
    j.startObject("data");
    if(client->scope.type == MonitorScope::RUN) {
        db->stmt("SELECT queuedAt,startedAt,completedAt, result, reason FROM builds WHERE name = ? AND number = ?")
        .bind(client->scope.job, client->scope.num)
        .fetch<time_t, time_t, time_t, int, std::string>([&](time_t queued, time_t started, time_t completed, int result, std::string reason) {
            j.set("queued", started-queued);
            j.set("started", started);
            j.set("completed", completed);
            j.set("result", to_string(RunState(result)));
            j.set("reason", reason);
        });
        if(const Run* run = activeRun(client->scope.job, client->scope.num)) {
            j.set("queued", run->startedAt - run->queuedAt);
            j.set("started", run->startedAt);
            j.set("reason", run->reason());
            j.set("result", to_string(RunState::RUNNING));
            db->stmt("SELECT completedAt - startedAt FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 1")
             .bind(run->name)
             .fetch<uint>([&](uint lastRuntime){
                j.set("etc", run->startedAt + lastRuntime);
            });
        }
        j.set("latestNum", int(buildNums[client->scope.job]));
        j.startArray("artifacts");
        populateArtifacts(j, client->scope.job, client->scope.num);
        j.EndArray();
    } else if(client->scope.type == MonitorScope::JOB) {
        j.startArray("recent");
        db->stmt("SELECT number,startedAt,completedAt,result,reason FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 25")
        .bind(client->scope.job)
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
        j.startArray("running");
        auto p = activeJobs.byJobName().equal_range(client->scope.job);
        for(auto it = p.first; it != p.second; ++it) {
            const std::shared_ptr<Run> run = *it;
            j.StartObject();
            j.set("number", run->build);
            j.set("node", run->node->name);
            j.set("started", run->startedAt);
            j.set("result", to_string(RunState::RUNNING));
            j.set("reason", run->reason());
            j.EndObject();
        }
        j.EndArray();
        int nQueued = 0;
        for(const auto& run : queuedJobs) {
            if (run->name == client->scope.job) {
                nQueued++;
            }
        }
        j.set("nQueued", nQueued);
        db->stmt("SELECT number,startedAt FROM builds WHERE name = ? AND result = ? ORDER BY completedAt DESC LIMIT 1")
        .bind(client->scope.job, int(RunState::SUCCESS))
        .fetch<int,time_t>([&](int build, time_t started){
            j.startObject("lastSuccess");
            j.set("number", build).set("started", started);
            j.EndObject();
        });
        db->stmt("SELECT number,startedAt FROM builds WHERE name = ? AND result <> ? ORDER BY completedAt DESC LIMIT 1")
        .bind(client->scope.job, int(RunState::SUCCESS))
        .fetch<int,time_t>([&](int build, time_t started){
            j.startObject("lastFailed");
            j.set("number", build).set("started", started);
            j.EndObject();
        });

    } else if(client->scope.type == MonitorScope::ALL) {
        j.startArray("jobs");
        db->stmt("SELECT name,number,startedAt,completedAt,result FROM builds GROUP BY name ORDER BY number DESC")
        .fetch<str,uint,time_t,time_t,int>([&](str name,uint number, time_t started, time_t completed, int result){
            j.StartObject();
            j.set("name", name);
            j.set("number", number);
            j.set("result", to_string(RunState(result)));
            j.set("started", started);
            j.set("completed", completed);
            j.startArray("tags");
            for(const str& t: jobTags[name]) {
                j.String(t.c_str());
            }
            j.EndArray();
            j.EndObject();
        });
        j.EndArray();
        j.startArray("running");
        for(const auto& run : activeJobs.byStartedAt()) {
            j.StartObject();
            j.set("name", run->name);
            j.set("number", run->build);
            j.set("node", run->node->name);
            j.set("started", run->startedAt);
            j.startArray("tags");
            for(const str& t: jobTags[run->name]) {
                j.String(t.c_str());
            }
            j.EndArray();
            j.EndObject();
        }
        j.EndArray();
    } else { // Home page
        j.startArray("recent");
        db->stmt("SELECT * FROM builds ORDER BY completedAt DESC LIMIT 15")
        .fetch<str,uint,str,time_t,time_t,time_t,int>([&](str name,uint build,str node,time_t,time_t started,time_t completed,int result){
            j.StartObject();
            j.set("name", name)
             .set("number", build)
             .set("node", node)
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
            j.set("node", run->node->name);
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
        for(const auto& it : nodes) {
            const Node& node = it.second;
            execTotal += node.numExecutors;
            execBusy += node.busyExecutors;
        }
        j.set("executorsTotal", execTotal);
        j.set("executorsBusy", execBusy);
        j.startArray("buildsPerDay");
        for(int i = 6; i >= 0; --i) {
            j.StartObject();
            db->stmt("SELECT result, COUNT(*) FROM builds WHERE completedAt > ? AND completedAt < ? GROUP by result")
                    .bind(86400*(time(nullptr)/86400 - i), 86400*(time(nullptr)/86400 - (i-1)))
                    .fetch<int,int>([&](int result, int num){
                j.set(to_string(RunState(result)).c_str(), num);
            });
            j.EndObject();
        }
        j.EndArray();
        j.startObject("buildsPerJob");
        db->stmt("SELECT name, COUNT(*) FROM builds WHERE completedAt > ? GROUP BY name")
                .bind(time(nullptr) - 86400)
                .fetch<str, int>([&](str job, int count){
            j.set(job.c_str(), count);
        });
        j.EndObject();
        j.startObject("timePerJob");
        db->stmt("SELECT name, AVG(completedAt-startedAt) FROM builds WHERE completedAt > ? GROUP BY name")
                .bind(time(nullptr) - 7 * 86400)
                .fetch<str, uint>([&](str job, uint time){
            j.set(job.c_str(), time);
        });
        j.EndObject();

    }
    j.EndObject();
    client->sendMessage(j.str());

}

Laminar::~Laminar() {
    delete db;
    delete srv;
}

void Laminar::run() {
    const char* listen_rpc = getenv("LAMINAR_BIND_RPC") ?: INTADDR_RPC_DEFAULT;
    const char* listen_http = getenv("LAMINAR_BIND_HTTP") ?: INTADDR_HTTP_DEFAULT;

    srv = new Server(*this, listen_rpc, listen_http);
    srv->addWatchPath(fs::path(fs::path(homeDir)/"cfg"/"nodes").string().c_str());
    srv->addWatchPath(fs::path(fs::path(homeDir)/"cfg"/"jobs").string().c_str());
    srv->start();
}

void Laminar::stop() {
    srv->stop();
}

bool Laminar::loadConfiguration() {
    if(const char* ndirs = getenv("LAMINAR_KEEP_RUNDIRS"))
        numKeepRunDirs = static_cast<uint>(atoi(ndirs));

    NodeMap nm;

    fs::path nodeCfg = fs::path(homeDir)/"cfg"/"nodes";

    if(fs::is_directory(nodeCfg)) {
        for(fs::directory_iterator it(nodeCfg); it != fs::directory_iterator(); ++it) {
            if(!fs::is_regular_file(it->status()) || it->path().extension() != ".conf")
                continue;

            StringMap conf = parseConfFile(it->path().string().c_str());

            Node node;
            node.name = it->path().stem().string();
            node.numExecutors = conf.get<int>("EXECUTORS", 6);

            std::string tags = conf.get<std::string>("TAGS");
            if(!tags.empty()) {
                std::istringstream iss(tags);
                std::set<std::string> tagList;
                std::string tag;
                while(std::getline(iss, tag, ','))
                    tagList.insert(tag);
                node.tags = tagList;
            }

            nm.emplace(node.name, std::move(node));
        }
    }

    if(nm.empty()) {
        // add a default node
        Node node;
        node.name = "";
        node.numExecutors = 6;
        nm.emplace("", std::move(node));
    }

    nodes = nm;

    fs::path jobsDir = fs::path(homeDir)/"cfg"/"jobs";
    if(fs::is_directory(jobsDir)) {
        for(fs::directory_iterator it(jobsDir); it != fs::directory_iterator(); ++it) {
            if(!fs::is_regular_file(it->status()) || it->path().extension() != ".conf")
                continue;

            StringMap conf = parseConfFile(it->path().string().c_str());

            std::string tags = conf.get<std::string>("TAGS");
            if(!tags.empty()) {
                std::istringstream iss(tags);
                std::set<std::string> tagList;
                std::string tag;
                while(std::getline(iss, tag, ','))
                    tagList.insert(tag);
                jobTags[it->path().stem().string()] = tagList;
            }

        }
    }

    return true;
}

std::shared_ptr<Run> Laminar::queueJob(std::string name, ParamMap params) {
    if(!fs::exists(fs::path(homeDir)/"cfg"/"jobs"/name+".run")) {
        LLOG(ERROR, "Non-existent job", name);
        return nullptr;
    }

    std::shared_ptr<Run> run = std::make_shared<Run>();
    run->name = name;
    run->queuedAt = time(nullptr);
    for(auto it = params.begin(); it != params.end();) {
        if(it->first[0] == '=') {
            if(it->first == "=parentJob") {
                run->parentName = it->second;
            } else if(it->first == "=parentBuild") {
                run->parentBuild = atoi(it->second.c_str());
            } else if(it->first == "=reason") {
                run->reasonMsg = it->second;
            } else {
                LLOG(ERROR, "Unknown internal job parameter", it->first);
            }
            it = params.erase(it);
        } else
            ++it;
    }
    run->params = params;
    queuedJobs.push_back(run);

    // notify clients
    Json j;
    j.set("type", "job_queued")
        .startObject("data")
        .set("name", name)
        .EndObject();
    const char* msg = j.str();
    for(LaminarClient* c : clients) {
        if(c->scope.wantsStatus(name))
            c->sendMessage(msg);
    }

    assignNewJobs();
    return run;
}

bool Laminar::stepRun(std::shared_ptr<Run> run) {
    bool complete = run->step();
    if(!complete) {
        srv->addDescriptor(run->fd, [this, run](const char* b,size_t n){
            handleRunLog(run, std::string(b,n));
        });
    }
    return complete;
}

void Laminar::handleRunLog(std::shared_ptr<Run> run, std::string s) {
    run->log += s;
    for(LaminarClient* c : clients) {
        if(c->scope.wantsLog(run->name, run->build))
            c->sendMessage(s);
    }
}

// Reaps a zombie and steps the corresponding Run to its next state.
// Should be called on SIGCHLD
void Laminar::reapChildren() {
    int ret = 0;
    pid_t pid;
    constexpr int bufsz = 1024;
    static thread_local char buf[bufsz];
    while((pid = waitpid(-1, &ret, WNOHANG)) > 0) {
        LLOG(INFO, "Reaping", pid);
        auto it = activeJobs.byPid().find(pid);
        std::shared_ptr<Run> run = *it;
        // The main event loop might schedule this SIGCHLD handler before the final
        // output handler (from addDescriptor). In that case, because it keeps a
        // shared_ptr to the run it would successfully add to the log output buffer,
        // but by then reapAdvance would have stored the log and ensured no-one cares.
        // Preempt this case by forcing a final (non-blocking) read here.
        for(ssize_t n = read(run->fd, buf, bufsz); n > 0; n = read(run->fd, buf, bufsz)) {
            handleRunLog(run, std::string(buf, static_cast<size_t>(n)));
        }
        bool completed = true;
        activeJobs.byPid().modify(it, [&](std::shared_ptr<Run> run){
            run->reaped(ret);
            completed = stepRun(run);
        });
        if(completed)
            run->complete();
    }

    assignNewJobs();
}

void Laminar::notifyConfigChanged()
{
    loadConfiguration();
    // config change may allow stuck jobs to dequeue
    assignNewJobs();
}

void Laminar::abortAll() {
    for(std::shared_ptr<Run> run : activeJobs) {
        run->abort();
    }
}

bool Laminar::nodeCanQueue(const Node& node, const Run& run) const {
    // if a node is too busy, it can't take the job
    if(node.busyExecutors >= node.numExecutors)
        return false;

    // if the node has no tags, allow the build
    if(node.tags.size() == 0)
        return true;

    auto it = jobTags.find(run.name);
    // if the job has no tags, it cannot be run on this node
    if(it == jobTags.end())
        return false;

    // otherwise, allow the build if job and node have a tag in common
    for(const std::string& tag : it->second) {
        if(node.tags.find(tag) != node.tags.end())
            return true;
    }

    return false;
}

void Laminar::assignNewJobs() {
    auto it = queuedJobs.begin();
    while(it != queuedJobs.end()) {
        bool assigned = false;
        for(auto& sn : nodes) {
            Node& node = sn.second;
            std::shared_ptr<Run> run = *it;
            if(nodeCanQueue(node, *run)) {
                fs::path cfgDir = fs::path(homeDir)/"cfg";
                boost::system::error_code err;

                // create a workspace for this job if it doesn't exist
                fs::path ws = fs::path(homeDir)/"run"/run->name/"workspace";
                if(!fs::exists(ws)) {
                    if(!fs::create_directories(ws, err)) {
                        LLOG(ERROR, "Could not create job workspace", run->name);
                        break;
                    }
                    // prepend the workspace init script
                    if(fs::exists(cfgDir/"jobs"/run->name+".init"))
                        run->addScript((cfgDir/"jobs"/run->name+".init").string(), ws.string());
                }

                uint buildNum = buildNums[run->name] + 1;
                // create the run directory
                fs::path rd = fs::path(homeDir)/"run"/run->name/std::to_string(buildNum);
                bool createWorkdir = true;
                if(fs::is_directory(rd)) {
                    LLOG(WARNING, "Working directory already exists, removing", rd.string());
                    fs::remove_all(rd, err);
                    if(err) {
                        LLOG(WARNING, "Failed to remove working directory", err.message());
                        createWorkdir = false;
                    }
                }
                if(createWorkdir && !fs::create_directory(rd, err)) {
                    LLOG(ERROR, "Could not create working directory", rd.string());
                    break;
                }
                run->runDir = rd.string();

                // create an archive directory
                fs::path archive = fs::path(homeDir)/"archive"/run->name/std::to_string(buildNum);
                if(fs::is_directory(archive)) {
                    LLOG(WARNING, "Archive directory already exists", archive.string());
                } else if(!fs::create_directories(archive)) {
                    LLOG(ERROR, "Could not create archive directory", archive.string());
                    break;
                }

                // add scripts
                // global before-run script
                if(fs::exists(cfgDir/"before"))
                    run->addScript((cfgDir/"before").string());
                // per-node before-run script
                if(fs::exists(cfgDir/"nodes"/node.name+".before"))
                    run->addScript((cfgDir/"nodes"/node.name+".before").string());
                // job before-run script
                if(fs::exists(cfgDir/"jobs"/run->name+".before"))
                    run->addScript((cfgDir/"jobs"/run->name+".before").string());
                // main run script. must exist.
                run->addScript((cfgDir/"jobs"/run->name+".run").string());
                // job after-run script
                if(fs::exists(cfgDir/"jobs"/run->name+".after"))
                    run->addScript((cfgDir/"jobs"/run->name+".after").string());
                // per-node after-run script
                if(fs::exists(cfgDir/"nodes"/node.name+".after"))
                    run->addScript((cfgDir/"nodes"/node.name+".after").string());
                // global after-run script
                if(fs::exists(cfgDir/"after"))
                    run->addScript((cfgDir/"after").string());

                // add environment files
                if(fs::exists(cfgDir/"env"))
                    run->addEnv((cfgDir/"env").string());
                if(fs::exists(cfgDir/"nodes"/node.name+".env"))
                    run->addEnv((cfgDir/"nodes"/node.name+".env").string());
                if(fs::exists(cfgDir/"jobs"/run->name+".env"))
                    run->addEnv((cfgDir/"jobs"/run->name+".env").string());

                // start the job
                node.busyExecutors++;
                run->node = &node;
                run->startedAt = time(nullptr);
                run->laminarHome = homeDir;
                run->build = buildNum;
                // set the last known result if exists
                db->stmt("SELECT result FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 1")
                 .bind(run->name)
                 .fetch<int>([=](int result){
                    run->lastResult = RunState(result);
                });
                // update next build number
                buildNums[run->name] = buildNum;

                LLOG(INFO, "Queued job to node", run->name, run->build, node.name);

                // notify clients
                Json j;
                j.set("type", "job_started")
                 .startObject("data")
                 .set("queueIndex", std::distance(it,queuedJobs.begin()))
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
                const char* msg = j.str();
                for(LaminarClient* c : clients) {
                    if(c->scope.wantsStatus(run->name, run->build)
                        // The run page also should know that another job has started
                        // (so maybe it can show a previously hidden "next" button).
                        // Hence this small hack:
                            || (c->scope.type == MonitorScope::Type::RUN && c->scope.job == run->name))
                        c->sendMessage(msg);
                }

                // setup run completion handler
                run->notifyCompletion = [this](Run* r) { runFinished(r); };

                // trigger the first step of the run
                if(stepRun(run)) {
                    // should never happen
                    LLOG(INFO, "No steps for run");
                    run->complete();
                }

                assigned = true;
                break;
            }
        }
        if(assigned) {
            activeJobs.insert(*it);
            it = queuedJobs.erase(it);
        } else
            ++it;

    }
}

void Laminar::runFinished(Run * r) {
    Node* node = r->node;

    node->busyExecutors--;
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
     .bind(r->name, r->build, node->name, r->queuedAt, r->startedAt, completedAt, int(r->result),
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
    const char* msg = j.str();
    for(LaminarClient* c : clients) {
        if(c->scope.wantsStatus(r->name, r->build))
            c->sendMessage(msg);
    }

    // notify the waiters
    for(LaminarWaiter* w : waiters) {
        w->complete(r);
    }

    // erase reference to run from activeJobs
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
        fs::path d = fs::path(homeDir)/"run"/r->name/std::to_string(i);
        // Once the directory does not exist, it's probably not worth checking
        // any further. 99% of the time this loop should only ever have 1 iteration
        // anyway so hence this (admittedly debatable) optimization.
        if(!fs::exists(d))
            break;
        fs::remove_all(d);
    }
}

// Small helper function to return the full contents of a file given its path.
// It reads in the whole file into the given string reference.
// This is a terrible way to serve files (especially large ones). Hopefully
// no-one uses this function and configures their webservers appropriately.
static bool slurp(fs::path path, std::string& output) {
    if(!fs::is_regular_file(path))
        return false;
    std::ifstream fstr(path.string());
    fstr.seekg(0, std::ios::end);
    ssize_t sz = fstr.tellg();
    if(fstr.good()) {
        output.resize(static_cast<size_t>(sz));
        fstr.seekg(0);
        fstr.read(&output[0], sz);
        return true;
    }
    return false;
}

bool Laminar::getArtefact(std::string path, std::string& result) {
    if(archiveUrl != ARCHIVE_URL_DEFAULT) {
        // we shouldn't have got here. Probably an invalid link.
        return false;
    }
    fs::path file(fs::path(homeDir)/"archive"/path);
    return slurp(file, result);
}

std::string Laminar::getCustomCss()
{
    fs::path file(fs::path(homeDir)/"custom"/"style.css");
    std::string result;
    slurp(file, result);
    return result;
}
