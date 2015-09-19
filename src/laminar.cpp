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
#include "laminar.h"
#include "server.h"
#include "conf.h"

#include <sys/wait.h>
#include <kj/debug.h>

#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace {

// rapidjson::Writer with a StringBuffer is used a lot in Laminar for
// preparing JSON messages to send to Websocket clients. A small wrapper
// class here reduces verbosity later for this common use case.
class Json : public rapidjson::Writer<rapidjson::StringBuffer> {
public:
    Json() : rapidjson::Writer<rapidjson::StringBuffer>(buf) { StartObject(); }
    template<typename T>
    Json& set(const char* key, T value);
    Json& startObject(const char* key) { String(key); StartObject(); return *this; }
    Json& startArray(const char* key) { String(key); StartArray(); return *this; }
    const char* str() { EndObject(); return buf.GetString(); }
private:
    rapidjson::StringBuffer buf;
};
template<> Json& Json::set(const char* key, const char* value) { String(key); String(value); return *this; }
template<> Json& Json::set(const char* key, std::string value) { String(key); String(value.c_str()); return *this; }
template<> Json& Json::set(const char* key, int value) { String(key); Int(value); return *this; }
template<> Json& Json::set(const char* key, time_t value) { String(key); Int64(value); return *this; }

}

namespace {
// Default values when none were supplied in $LAMINAR_CONF_FILE (/etc/laminar.conf)
constexpr const char* INTADDR_RPC_DEFAULT = "unix:\0laminar";
constexpr const char* INTADDR_HTTP_DEFAULT = "*:8080";
constexpr const char* BASE_CFG_DIR = "/home/og/dev/laminar/cfg";
}

typedef std::string str;

Laminar::Laminar() {
    homeDir = getenv("LAMINAR_HOME") ?: "/var/lib/laminar";

    db = new Database((fs::path(homeDir)/"laminar.sqlite").string().c_str());
    // Prepare database for first use
    // TODO: error handling
    db->exec("CREATE TABLE IF NOT EXISTS builds("
            "name TEXT, number INT UNSIGNED, node TEXT, queuedAt INT, startedAt INT, completedAt INT, result INT, output TEXT, parentJob TEXT, parentBuild INT, reason TEXT)");

    // retrieve the last build numbers
    db->stmt("SELECT name, MAX(number) FROM builds GROUP BY name")
    .fetch<str,int>([this](str name, int build){
        buildNums[name] = build;
    });

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

bool Laminar::setParam(std::string job, int buildNum, std::string param, std::string value) {
    auto it = activeJobs.get<1>().find(std::make_tuple(job, buildNum));
    if(it == activeJobs.get<1>().end())
        return false;
    std::shared_ptr<Run> run = *it;
    run->params[param] = value;
    return true;
}

void Laminar::sendStatus(LaminarClient* client) {
    if(client->scope.type == MonitorScope::LOG) {
        // If the requested job is currently in progress
        auto it = activeJobs.get<1>().find(std::make_tuple(client->scope.job, client->scope.num));
        if(it != activeJobs.get<1>().end()) {
            client->sendMessage((*it)->log.c_str());
        } else { // it must be finished, fetch it from the database
            db->stmt("SELECT output FROM builds WHERE name = ? AND number = ?")
              .bind(client->scope.job, client->scope.num)
              .fetch<const char*>([=](const char* log) {
                client->sendMessage(log);
            });
        }
    } else if(client->scope.type == MonitorScope::RUN) {
        Json j;
        j.set("type", "status");
        j.startObject("data");
        db->stmt("SELECT startedAt, result, reason FROM builds WHERE name = ? AND number = ?")
        .bind(client->scope.job, client->scope.num)
        .fetch<time_t, int, std::string>([&](time_t started, int result, std::string reason) {
            j.set("started", started);
            j.set("result", to_string(RunState(result)));
            j.set("reason", reason);
        });
        j.EndObject();
        client->sendMessage(j.str());
    } else if(client->scope.type == MonitorScope::JOB) {
        Json j;
        j.set("type", "status");
        j.startObject("data");
        j.startArray("recent");
        db->stmt("SELECT * FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 5")
        .bind(client->scope.job)
        .fetch<str,int,str,time_t,time_t,time_t,int>([&](str name,int build,str node,time_t,time_t started,time_t completed,int result){
            j.StartObject();
            j.set("name", name)
             .set("number", build)
             .set("node", node)
             .set("duration", completed - started)
             .set("started", started)
             .set("result", to_string(RunState(result)))
             .EndObject();
        });
        j.EndArray();
        j.startArray("running");
        auto p = activeJobs.get<4>().equal_range(client->scope.job);
        for(auto it = p.first; it != p.second; ++it) {
            const std::shared_ptr<Run> run = *it;
            j.StartObject();
            j.set("name", run->name);
            j.set("number", run->build);
            j.set("node", run->node->name);
            j.set("started", run->startedAt);
            j.EndObject();
        }
        j.EndArray();
        j.startArray("queued");
        for(const std::shared_ptr<Run> run : queuedJobs) {
            if (run->name == client->scope.job) {
                j.StartObject();
                j.set("name", run->name);
                j.EndObject();
            }
        }
        j.EndArray();
        j.EndObject();
        client->sendMessage(j.str());

    } else if(client->scope.type == MonitorScope::ALL) {
        Json j;
        j.set("type", "status");
        j.startObject("data");
        j.startArray("jobs");
        db->stmt("SELECT name FROM builds GROUP BY name")
        .fetch<str>([&](str name){
            j.StartObject();
            j.set("name", name)
             .EndObject();
        });
        j.EndArray();
        j.EndObject();
        client->sendMessage(j.str());
    } else { // Home page
        Json j;
        j.set("type", "status");
        j.startObject("data");
        j.startArray("recent");
        db->stmt("SELECT * FROM builds ORDER BY completedAt DESC LIMIT 5")
        .fetch<str,int,str,time_t,time_t,time_t,int>([&](str name,int build,str node,time_t,time_t started,time_t completed,int result){
            j.StartObject();
            j.set("name", name)
             .set("number", build)
             .set("node", node)
             .set("duration", completed - started)
             .set("started", started)
             .set("result", to_string(RunState(result)))
             .EndObject();
        });
        j.EndArray();
        j.startArray("running");
        for(const std::shared_ptr<Run> run : activeJobs.get<3>()) {
            j.StartObject();
            j.set("name", run->name);
            j.set("number", run->build);
            j.set("node", run->node->name);
            j.set("started", run->startedAt);
            j.EndObject();
        }
        j.EndArray();
        j.startArray("queued");
        for(const std::shared_ptr<Run> run : queuedJobs) {
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
                    .bind(86400*(time(0)/86400 - i), 86400*(time(0)/86400 - (i-1)))
                    .fetch<int,int>([&](int result, int num){
                j.set(to_string(RunState(result)).c_str(), num);
            });
            j.EndObject();
        }
        j.EndArray();
        j.startObject("buildsPerJob");
        db->stmt("SELECT name, COUNT(*) FROM builds WHERE completedAt > ? GROUP BY name")
                .bind(time(0) - 86400)
                .fetch<str, int>([&](str job, int count){
            j.set(job.c_str(), count);
        });
        j.EndObject();

        j.EndObject();
        client->sendMessage(j.str());
    }
}

Laminar::~Laminar() {
    delete db;
    delete srv;
}

void Laminar::run() {
    const char* listen_rpc = getenv("LAMINAR_BIND_RPC") ?: INTADDR_RPC_DEFAULT;
    const char* listen_http = getenv("LAMINAR_BIND_HTTP") ?: INTADDR_HTTP_DEFAULT;

    srv = new Server(*this, listen_rpc, listen_http);

    srv->start();
}

void Laminar::stop() {
    clients.clear();
    srv->stop();
}

bool Laminar::loadConfiguration() {
    NodeMap nm;

    fs::path nodeCfg = fs::path(homeDir)/"cfg"/"nodes";

    if(fs::is_directory(nodeCfg)) {
        fs::directory_iterator dit(nodeCfg);
        for(fs::directory_entry& it : dit) {
            if(!fs::is_directory(it.status()))
                continue;

            fs::directory_entry config(it.path()/"config");
            if(!fs::is_regular_file(config.status()))
                continue;

            StringMap conf = parseConfFile(config.path().string().c_str());

            Node node;
            node.name = it.path().filename().string();
            node.numExecutors = conf.get<int>("EXECUTORS", 6);
            nm.emplace(node.name, std::move(node));
        }
    }

    if(nm.empty()) {
        // add a default node
        Node node;
        node.name = "default";
        node.numExecutors = 6;
        nm.emplace("default", std::move(node));
    }

    nodes = nm;

    return true;
}

std::shared_ptr<Run> Laminar::queueJob(std::string name, ParamMap params) {
    if(!fs::exists(fs::path(homeDir)/"cfg"/"jobs"/name))
        return nullptr;

    // attempt to create a workspace for this job if it doesn't exist
    if(!fs::exists(fs::path(homeDir)/"run"/name/"workspace")) {
        if(!fs::create_directories(fs::path(homeDir)/"run"/name/"workspace")) {
            KJ_LOG(ERROR, "Could not create job workspace", name);
            return nullptr;
        }
    }

    std::shared_ptr<Run> run = std::make_shared<Run>();
    run->name = name;
    run->queuedAt = time(0);
    for(auto it = params.begin(); it != params.end();) {
        if(it->first[0] == '=') {
            if(it->first == "=parentJob") {
                run->parentName = it->second;
            } else if(it->first == "=parentBuild") {
                run->parentBuild = atoi(it->second.c_str());
            } else if(it->first == "=reason") {
                run->reasonMsg = it->second;
            } else {
                KJ_LOG(ERROR, "Unknown internal job parameter", it->first);
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

kj::Promise<RunState> Laminar::waitForRun(std::string name, int buildNum) {
    auto it = activeJobs.get<1>().find(std::make_tuple(name, buildNum));
    if(it == activeJobs.get<1>().end())
        return RunState::UNKNOWN;
    return waitForRun(it->get());
}

kj::Promise<RunState> Laminar::waitForRun(const Run* run) {
    waiters[run].emplace_back(Waiter{});
    return waiters[run].back().takePromise();
}

bool Laminar::stepRun(std::shared_ptr<Run> run) {
    bool complete = run->step();
    if(!complete) {
        srv->addProcess(run->fd, [=](char* b,size_t n){
            std::string s(b,n);
            run->log += s;
            for(LaminarClient* c : clients) {
                if(c->scope.wantsLog(run->name, run->build))
                    c->sendMessage(s);
            }
        }, [this,run](){ reapAdvance();});
    }
    return complete;
}

void Laminar::reapAdvance() {
    int ret = 0;
    // TODO: If we pass WNOHANG here for better asynchronicity, how do
    // we re-schedule a poll to wait for finished child processes?
    pid_t pid = waitpid(-1, &ret, 0);
    // TODO: handle signalled child processes
    if(pid > 0) {
        KJ_LOG(INFO, "Reaping", pid);
        auto it = activeJobs.get<0>().find(pid);
        std::shared_ptr<Run> run = *it;
        bool completed = true;
        activeJobs.get<0>().modify(it, [&](std::shared_ptr<Run> run){
            close(run->fd);
            run->reaped(ret);
            completed = stepRun(run);
        });
        if(completed)
            run->complete();
    }
    assignNewJobs();
}

void Laminar::assignNewJobs() {
    auto it = queuedJobs.begin();
    while(it != queuedJobs.end()) {
        bool assigned = false;
        for(auto& sn : nodes) {
            Node& node = sn.second;
            std::shared_ptr<Run> run = *it;
            if(node.queue(*run)) {
                node.busyExecutors++;
                run->node = &node;
                run->startedAt = time(0);
                run->build = ++buildNums[run->name];
                run->laminarHome = homeDir;
                // set the last known result if exists
                db->stmt("SELECT result FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 1")
                 .bind(run->name)
                 .fetch<int>([=](int result){
                    run->lastResult = RunState(result);
                });

                // create a working directory (different to a workspace!)
                fs::path wd = fs::path(homeDir)/"run"/run->name/std::to_string(run->build);
                if(!fs::create_directory(wd)) {
                    KJ_LOG(ERROR, "Could not create working directory", wd.string());
                    break;
                }
                run->wd = wd.string();
                // create an archive directory
                fs::path archive = fs::path(homeDir)/"archive"/run->name/std::to_string(run->build);
                if(!fs::create_directories(archive)) {
                    KJ_LOG(ERROR, "Could not create archive directory", archive.string());
                    break;
                }

                // add scripts
                fs::path cfgDir = fs::path(homeDir)/"cfg";
                // global before-run script
                if(fs::exists(cfgDir/"before"))
                    run->addScript((cfgDir/"before").string());
                // per-node before-run script
                if(fs::exists(cfgDir/"nodes"/node.name/"before"))
                    run->addScript((cfgDir/"before").string());
                // job before-run script
                if(fs::exists(cfgDir/"jobs"/run->name/"before"))
                    run->addScript((cfgDir/"jobs"/run->name/"before").string());
                // main run script. must exist.
                run->addScript((cfgDir/"jobs"/run->name/"run").string());
                // job after-run script
                if(fs::exists(cfgDir/"jobs"/run->name/"after"))
                    run->addScript((cfgDir/"jobs"/run->name/"after").string());
                // per-node after-run script
                if(fs::exists(cfgDir/"nodes"/node.name/"after"))
                    run->addScript((cfgDir/"nodes"/node.name/"after").string());
                // global after-run script
                if(fs::exists(cfgDir/"after"))
                    run->addScript((cfgDir/"after").string());

                KJ_LOG(INFO, "Queued job to node", run->name, run->build, node.name);

                // notify clients
                Json j;
                j.set("type", "job_started")
                 .startObject("data")
                 .set("queueIndex", std::distance(it,queuedJobs.begin()))
                 .set("name", run->name)
                 .set("started", run->startedAt)
                 .set("number", run->build)
                 .set("reason", run->reason());
                db->stmt("SELECT completedAt - startedAt FROM builds WHERE name = ? ORDER BY completedAt DESC LIMIT 1")
                 .bind(run->name)
                 .fetch<int>([&](int etc){
                    j.set("etc", time(0) + etc);
                });
                j.EndObject();
                const char* msg = j.str();
                for(LaminarClient* c : clients) {
                    if(c->scope.wantsStatus(run->name, run->build))
                        c->sendMessage(msg);
                }

                // setup run completion handler
                run->notifyCompletion = [&](const Run* r) {
                    node.busyExecutors--;
                    KJ_LOG(INFO, "Run completed", r->name, to_string(r->result));
                    time_t completedAt = time(0);
                    db->stmt("INSERT INTO builds VALUES(?,?,?,?,?,?,?,?,?,?,?)")
                     .bind(r->name, r->build, node.name, r->queuedAt, r->startedAt, completedAt, int(r->result),
                           r->log, r->parentName, r->parentBuild, r->reason())
                     .exec();

                    // notify clients
                    Json j;
                    j.set("type", "job_completed")
                            .startObject("data")
                            .set("name", r->name)
                            .set("number", r->build)
                            .set("duration", completedAt - r->startedAt)
                            .set("started", r->startedAt)
                            .set("result", to_string(r->result))
                            .EndObject();
                    const char* msg = j.str();
                    for(LaminarClient* c : clients) {
                        if(c->scope.wantsStatus(r->name, r->build))
                            c->sendMessage(msg);
                    }

                    // wake the waiters
                    for(Waiter& waiter : waiters[r])
                        waiter.release(r->result);
                    waiters.erase(r);

                    // will delete the job
                    activeJobs.get<2>().erase(r);
                };

                // trigger the first step of the run
                if(stepRun(run)) {
                    // should never happen
                    KJ_LOG(INFO, "No steps for run");
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
