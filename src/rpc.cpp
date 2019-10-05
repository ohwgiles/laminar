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
#include "rpc.h"
#include "laminar.capnp.h"
#include "laminar.h"
#include "log.h"

namespace {

// Used for returning run state to RPC clients
LaminarCi::JobResult fromRunState(RunState state) {
    switch(state) {
    case RunState::SUCCESS: return LaminarCi::JobResult::SUCCESS;
    case RunState::FAILED:  return LaminarCi::JobResult::FAILED;
    case RunState::ABORTED: return LaminarCi::JobResult::ABORTED;
    default:
        return LaminarCi::JobResult::UNKNOWN;
    }
}

}
// This is the implementation of the Laminar Cap'n Proto RPC interface.
// As such, it implements the pure virtual interface generated from
// laminar.capnp with calls to the primary Laminar class
class RpcImpl : public LaminarCi::Server {
public:
    RpcImpl(Laminar& l) :
        LaminarCi::Server(),
        laminar(l)
    {
    }

    virtual ~RpcImpl() {
    }

    // Queue a job, without waiting for it to start
    kj::Promise<void> queue(QueueContext context) override {
        std::string jobName = context.getParams().getJobName();
        LLOG(INFO, "RPC queue", jobName);
        LaminarCi::MethodResult result = laminar.queueJob(jobName, params(context.getParams().getParams()))
                ? LaminarCi::MethodResult::SUCCESS
                : LaminarCi::MethodResult::FAILED;
        context.getResults().setResult(result);
        return kj::READY_NOW;
    }

    // Start a job, without waiting for it to finish
    kj::Promise<void> start(StartContext context) override {
        std::string jobName = context.getParams().getJobName();
        LLOG(INFO, "RPC start", jobName);
        std::shared_ptr<Run> run = laminar.queueJob(jobName, params(context.getParams().getParams()));
        if(Run* r = run.get()) {
            return r->whenStarted().then([context,r]() mutable {
                context.getResults().setResult(LaminarCi::MethodResult::SUCCESS);
                context.getResults().setBuildNum(r->build);
            });
        } else {
            context.getResults().setResult(LaminarCi::MethodResult::FAILED);
            return kj::READY_NOW;
        }
    }

    // Start a job and wait for the result
    kj::Promise<void> run(RunContext context) override {
        std::string jobName = context.getParams().getJobName();
        LLOG(INFO, "RPC run", jobName);
        std::shared_ptr<Run> run = laminar.queueJob(jobName, params(context.getParams().getParams()));
        if(Run* r = run.get()) {
            return r->whenFinished().then([context,r](RunState state) mutable {
                context.getResults().setResult(fromRunState(state));
                context.getResults().setBuildNum(r->build);
            });
        } else {
            context.getResults().setResult(LaminarCi::JobResult::UNKNOWN);
            return kj::READY_NOW;
        }
    }

    // Set a parameter on a running build
    kj::Promise<void> set(SetContext context) override {
        std::string jobName = context.getParams().getRun().getJob();
        uint buildNum = context.getParams().getRun().getBuildNum();
        LLOG(INFO, "RPC set", jobName, buildNum);

        LaminarCi::MethodResult result = laminar.setParam(jobName, buildNum,
            context.getParams().getParam().getName(), context.getParams().getParam().getValue())
                ? LaminarCi::MethodResult::SUCCESS
                : LaminarCi::MethodResult::FAILED;
        context.getResults().setResult(result);
        return kj::READY_NOW;
    }

    // List jobs in queue
    kj::Promise<void> listQueued(ListQueuedContext context) override {
        const std::list<std::shared_ptr<Run>>& queue = laminar.listQueuedJobs();
        auto res = context.getResults().initResult(queue.size());
        int i = 0;
        for(auto it : queue) {
            res.set(i++, it->name);
        }
        return kj::READY_NOW;
    }

    // List running jobs
    kj::Promise<void> listRunning(ListRunningContext context) override {
        const RunSet& active = laminar.listRunningJobs();
        auto res = context.getResults().initResult(active.size());
        int i = 0;
        for(auto it : active) {
            res[i].setJob(it->name);
            res[i].setBuildNum(it->build);
            i++;
        }
        return kj::READY_NOW;
    }

    // List known jobs
    kj::Promise<void> listKnown(ListKnownContext context) override {
        std::list<std::string> known = laminar.listKnownJobs();
        auto res = context.getResults().initResult(known.size());
        int i = 0;
        for(auto it : known) {
            res.set(i++, it);
        }
        return kj::READY_NOW;
    }

    kj::Promise<void> abort(AbortContext context) override {
        std::string jobName = context.getParams().getRun().getJob();
        uint buildNum = context.getParams().getRun().getBuildNum();
        LLOG(INFO, "RPC abort", jobName, buildNum);
        LaminarCi::MethodResult result = laminar.abort(jobName, buildNum)
                ? LaminarCi::MethodResult::SUCCESS
                : LaminarCi::MethodResult::FAILED;
        context.getResults().setResult(result);
        return kj::READY_NOW;
    }

private:
    // Helper to convert an RPC parameter list to a hash map
    ParamMap params(const capnp::List<LaminarCi::JobParam>::Reader& paramReader) {
        ParamMap res;
        for(auto p : paramReader) {
            res[p.getName().cStr()] = p.getValue().cStr();
        }
        return res;
    }

    Laminar& laminar;
    std::unordered_map<const Run*, std::list<kj::PromiseFulfillerPair<RunState>>> runWaiters;
};

Rpc::Rpc(Laminar& li) :
    rpcInterface(kj::heap<RpcImpl>(li))
{}

// Context for an RPC connection
struct RpcConnection {
    RpcConnection(kj::Own<kj::AsyncIoStream>&& stream,
                  capnp::Capability::Client bootstrap,
                  capnp::ReaderOptions readerOpts) :
        stream(kj::mv(stream)),
        network(*this->stream, capnp::rpc::twoparty::Side::SERVER, readerOpts),
        rpcSystem(capnp::makeRpcServer(network, bootstrap))
    {
    }
    kj::Own<kj::AsyncIoStream> stream;
    capnp::TwoPartyVatNetwork network;
    capnp::RpcSystem<capnp::rpc::twoparty::VatId> rpcSystem;
};

kj::Promise<void> Rpc::accept(kj::Own<kj::AsyncIoStream>&& connection) {
    auto server = kj::heap<RpcConnection>(kj::mv(connection), rpcInterface, capnp::ReaderOptions());
    return server->network.onDisconnect().attach(kj::mv(server));
}
