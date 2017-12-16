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
#ifndef INTERFACE_H
#define INTERFACE_H

#include "run.h"

#include <string>
#include <memory>
#include <unordered_map>

typedef std::unordered_map<std::string, std::string> ParamMap;

// Simple struct to define which information a frontend client is interested
// in, both in initial request phase and real-time updates. It corresponds
// loosely to frontend URLs
struct MonitorScope {
    enum Type {
        HOME, // home page: recent builds and statistics
        ALL,  // browse jobs
        JOB,  // a specific job page
        RUN,  // a specific run page
        LOG   // a run's log page
    };

    MonitorScope(Type type = HOME, std::string job = std::string(), int num = 0) :
        type(type),
        job(job),
        num(num)
    {}

    // whether this scope wants status information about the given job or run
    bool wantsStatus(std::string ajob, int anum = 0) const {
        if(type == HOME || type == ALL) return true;
        if(type == JOB) return ajob == job;
        if(type == RUN) return ajob == job && anum == num;
        return false;
    }

    bool wantsLog(std::string ajob, int anum) const {
        return type == LOG && ajob == job && anum == num;
    }

    Type type;
    std::string job;
    int num = 0;
};

// Represents a (websocket) client that wants to be notified about events
// matching the supplied scope. Pass instances of this to LaminarInterface
// registerClient and deregisterClient
struct LaminarClient {
    virtual void sendMessage(std::string payload) = 0;
    MonitorScope scope;
};

// Represents a (rpc) client that wants to be notified about run completion.
// Pass instances of this to LaminarInterface registerWaiter and
// deregisterWaiter
struct LaminarWaiter {
    virtual void complete(const Run*) = 0;
};

// The interface connecting the network layer to the application business
// logic. These methods fulfil the requirements of both the HTTP/Websocket
// and RPC interfaces.
struct LaminarInterface {
    // Queues a job, returns immediately. Return value will be nullptr if
    // the supplied name is not a known job.
    virtual std::shared_ptr<Run> queueJob(std::string name, ParamMap params = ParamMap()) = 0;

    // Register a client (but don't give up ownership). The client will be
    // notified with a JSON message of any events matching its scope
    // (see LaminarClient and MonitorScope above)
    virtual void registerClient(LaminarClient* client) = 0;

    // Call this before destroying a client so that Laminar doesn't try
    // to call LaminarClient::sendMessage on invalid data
    virtual void deregisterClient(LaminarClient* client) = 0;

    // Register a waiter (but don't give up ownership). The waiter will be
    // notified with a callback of any run completion (see LaminarWaiter above)
    virtual void registerWaiter(LaminarWaiter* waiter) = 0;

    // Call this before destroying a waiter so that Laminar doesn't try
    // to call LaminarWaiter::complete on invalid data
    virtual void deregisterWaiter(LaminarWaiter* waiter) = 0;

    // Synchronously send a snapshot of the current status to the given
    // client (as governed by the client's MonitorScope). This is called on
    // initial websocket connect.
    virtual void sendStatus(LaminarClient* client) = 0;

    // Implements the laminar client interface allowing the setting of
    // arbitrary parameters on a run (usually itself) to be available in
    // the environment of subsequent scripts.
    virtual bool setParam(std::string job, int buildNum, std::string param, std::string value) = 0;

    // Fetches the content of an artifact given its filename relative to
    // $LAMINAR_HOME/archive. This shouldn't be used, because the sysadmin
    // should have configured a real webserver to serve these things.
    virtual bool getArtefact(std::string path, std::string& result) = 0;
};

#endif // INTERFACE_H

