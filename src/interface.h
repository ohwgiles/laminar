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
#ifndef LAMINAR_INTERFACE_H_
#define LAMINAR_INTERFACE_H_

#include "run.h"

#include <string>
#include <memory>
#include <unordered_map>

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

    MonitorScope(Type type = HOME, std::string job = std::string(), uint num = 0) :
        type(type),
        job(job),
        num(num),
        page(0),
        field("number"),
        order_desc(true)
    {}

    // whether this scope wants status information about the given job or run
    bool wantsStatus(std::string ajob, uint anum = 0) const {
        if(type == HOME || type == ALL) return true;
        if(type == JOB) return ajob == job;
        if(type == RUN) return ajob == job && anum == num;
        return false;
    }

    bool wantsLog(std::string ajob, uint anum) const {
        return type == LOG && ajob == job && anum == num;
    }

    Type type;
    std::string job;
    uint num = 0;
    // sorting
    uint page = 0;
    std::string field;
    bool order_desc;
};

// Represents a (websocket) client that wants to be notified about events
// matching the supplied scope. Pass instances of this to LaminarInterface
// registerClient and deregisterClient
struct LaminarClient {
    virtual ~LaminarClient() noexcept(false) {}
    virtual void sendMessage(std::string payload) = 0;
    MonitorScope scope;
};

// Represents a (rpc) client that wants to be notified about run completion.
// Pass instances of this to LaminarInterface registerWaiter and
// deregisterWaiter
struct LaminarWaiter {
    virtual ~LaminarWaiter() =default;
    virtual void complete(const Run*) = 0;
};

// Represents a file mapped in memory. Used to serve artefacts
struct MappedFile {
    virtual ~MappedFile() =default;
    virtual const void* address() = 0;
    virtual size_t size() = 0;
};

// The interface connecting the network layer to the application business
// logic. These methods fulfil the requirements of both the HTTP/Websocket
// and RPC interfaces.
struct LaminarInterface {
    virtual ~LaminarInterface() {}

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
    virtual bool setParam(std::string job, uint buildNum, std::string param, std::string value) = 0;

    // Gets the list of jobs currently waiting in the execution queue
    virtual const std::list<std::shared_ptr<Run>>& listQueuedJobs() = 0;

    // Gets the list of currently executing jobs
    virtual const RunSet& listRunningJobs() = 0;

    // Gets the list of known jobs - scans cfg/jobs for *.run files
    virtual std::list<std::string> listKnownJobs() = 0;

    // Fetches the content of an artifact given its filename relative to
    // $LAMINAR_HOME/archive. Ideally, this would instead be served by a
    // proper web server which handles this url.
    virtual kj::Maybe<kj::Own<const kj::ReadableFile>> getArtefact(std::string path) = 0;

    // Given the name of a job, populate the provided string reference with
    // SVG content describing the last known state of the job. Returns false
    // if the job is unknown.
    virtual bool handleBadgeRequest(std::string job, std::string& badge) = 0;

    // Fetches the content of $LAMINAR_HOME/custom/style.css or an empty
    // string. Ideally, this would instead be served by a proper web server
    // which handles this url.
    virtual std::string getCustomCss() = 0;

    // Aborts a single job
    virtual bool abort(std::string job, uint buildNum) = 0;

    // Abort all running jobs
    virtual void abortAll() = 0;

    // Callback to handle a configuration modification notification
    virtual void notifyConfigChanged() = 0;
};

#endif // LAMINAR_INTERFACE_H_

