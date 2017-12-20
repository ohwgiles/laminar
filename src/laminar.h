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
#ifndef LAMINAR_LAMINAR_H_
#define LAMINAR_LAMINAR_H_

#include "interface.h"
#include "run.h"
#include "node.h"
#include "database.h"

#include <unordered_map>

// Node name to node object map
typedef std::unordered_map<std::string,Node> NodeMap;

struct Server;
class Json;

// The main class implementing the application's business logic.
// It owns a Server to manage the HTTP/websocket and Cap'n Proto RPC
// interfaces and communicates via the LaminarInterface methods and
// the LaminarClient objects (see interface.h)
class Laminar final : public LaminarInterface {
public:
    Laminar();
    ~Laminar();

    // Runs the application forever
    void run();
    // Call this in a signal handler to make run() return
    void stop();

    // Implementations of LaminarInterface
    std::shared_ptr<Run> queueJob(std::string name, ParamMap params = ParamMap()) override;
    void registerClient(LaminarClient* client) override;
    void deregisterClient(LaminarClient* client) override;
    void registerWaiter(LaminarWaiter* waiter) override;
    void deregisterWaiter(LaminarWaiter* waiter) override;

    void sendStatus(LaminarClient* client) override;
    bool setParam(std::string job, int buildNum, std::string param, std::string value) override;
    bool getArtefact(std::string path, std::string& result) override;

private:
    bool loadConfiguration();
    void reapAdvance();
    void assignNewJobs();
    bool stepRun(std::shared_ptr<Run> run);
    void handleRunLog(std::shared_ptr<Run> run, std::string log);
    void runFinished(Run*);
    bool nodeCanQueue(const Node&, const Run&) const;
    // expects that Json has started an array
    void populateArtifacts(Json& out, std::string job, int num) const;

    Run* activeRun(std::string name, int num) {
        auto it = activeJobs.byRun().find(boost::make_tuple(name, num));
        return it == activeJobs.byRun().end() ? nullptr : it->get();
    }

    std::list<std::shared_ptr<Run>> queuedJobs;

    std::unordered_map<std::string, uint> buildNums;

    std::unordered_map<std::string, std::set<std::string>> jobTags;

    RunSet activeJobs;
    Database* db;
    Server* srv;
    NodeMap nodes;
    std::string homeDir;
    std::set<LaminarClient*> clients;
    std::set<LaminarWaiter*> waiters;
    uint numKeepRunDirs;
    std::string archiveUrl;
};

#endif // LAMINAR_LAMINAR_H_
