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
#include "server.h"
#include "interface.h"
#include "laminar.capnp.h"
#include "resources.h"
#include "log.h"

#include <capnp/ez-rpc.h>
#include <capnp/rpc-twoparty.h>
#include <capnp/rpc.capnp.h>
#include <kj/async-io.h>
#include <kj/threadlocal.h>

#include <websocketpp/config/core.hpp>
#include <websocketpp/server.hpp>

#include <sys/eventfd.h>

// Size of buffer used to read from file descriptors. Should be
// a multiple of sizeof(struct signalfd_siginfo) == 128
#define PROC_IO_BUFSIZE 4096

// Configuration struct for the websocketpp template library.
struct wsconfig : public websocketpp::config::core {
//    static const websocketpp::log::level elog_level =
//        websocketpp::log::elevel::info;

//    static const websocketpp::log::level alog_level =
//        websocketpp::log::alevel::access_core |
//        websocketpp::log::alevel::message_payload ;

    static const websocketpp::log::level elog_level =
        websocketpp::log::elevel::none;

    static const websocketpp::log::level alog_level =
        websocketpp::log::alevel::none;

    typedef struct { LaminarClient* lc; } connection_base;
};
typedef websocketpp::server<wsconfig> websocket;

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
// laminar.capnp with calls to the LaminarInterface
class RpcImpl : public LaminarCi::Server, public LaminarWaiter {
public:
    RpcImpl(LaminarInterface& l) :
        LaminarCi::Server(),
        laminar(l)
    {
        laminar.registerWaiter(this);
    }

    ~RpcImpl() {
        laminar.deregisterWaiter(this);
    }

    // Start a job, without waiting for it to finish
    kj::Promise<void> trigger(TriggerContext context) override {
        std::string jobName = context.getParams().getJobName();
        LLOG(INFO, "RPC trigger", jobName);
        ParamMap params;
        for(auto p : context.getParams().getParams()) {
            params[p.getName().cStr()] = p.getValue().cStr();
        }
        LaminarCi::MethodResult result = laminar.queueJob(jobName, params)
                ? LaminarCi::MethodResult::SUCCESS
                : LaminarCi::MethodResult::FAILED;
        context.getResults().setResult(result);
        return kj::READY_NOW;
    }

    // Start a job and wait for the result
    kj::Promise<void> start(StartContext context) override {
        std::string jobName = context.getParams().getJobName();
        LLOG(INFO, "RPC start", jobName);
        ParamMap params;
        for(auto p : context.getParams().getParams()) {
            params[p.getName().cStr()] = p.getValue().cStr();
        }
        std::shared_ptr<Run> run = laminar.queueJob(jobName, params);
        if(const Run* r = run.get()) {
            runWaiters[r].emplace_back(kj::newPromiseAndFulfiller<RunState>());
            return runWaiters[r].back().promise.then([context](RunState state) mutable {
                context.getResults().setResult(fromRunState(state));
            });
        } else {
            context.getResults().setResult(LaminarCi::JobResult::UNKNOWN);
            return kj::READY_NOW;
        }
    }

    // Set a parameter on a running build
    kj::Promise<void> set(SetContext context) override {
        std::string jobName = context.getParams().getJobName();
        int buildNum = context.getParams().getBuildNum();
        LLOG(INFO, "RPC set", jobName, buildNum);

        LaminarCi::MethodResult result = laminar.setParam(jobName, buildNum,
            context.getParams().getParam().getName(), context.getParams().getParam().getValue())
                ? LaminarCi::MethodResult::SUCCESS
                : LaminarCi::MethodResult::FAILED;
        context.getResults().setResult(result);
        return kj::READY_NOW;
    }

    // Take a named lock
    kj::Promise<void> lock(LockContext context) override {
        std::string lockName = context.getParams().getLockName();
        LLOG(INFO, "RPC lock", lockName);
        auto& lockList = locks[lockName];
        lockList.emplace_back(kj::newPromiseAndFulfiller<void>());
        if(lockList.size() == 1)
            lockList.front().fulfiller->fulfill();
        return std::move(lockList.back().promise);
    }

    // Release a named lock
    kj::Promise<void> release(ReleaseContext context) override {
        std::string lockName = context.getParams().getLockName();
        LLOG(INFO, "RPC release", lockName);
        auto& lockList = locks[lockName];
        if(lockList.size() == 0) {
            LLOG(INFO, "Attempt to release unheld lock", lockName);
            return kj::READY_NOW;
        }
        lockList.erase(lockList.begin());
        if(lockList.size() > 0)
            lockList.front().fulfiller->fulfill();
        return kj::READY_NOW;
    }
private:
    // Implements LaminarWaiter::complete
    void complete(const Run* r) override {
        for(kj::PromiseFulfillerPair<RunState>& w : runWaiters[r])
            w.fulfiller->fulfill(RunState(r->result));
        runWaiters.erase(r);
    }
private:
    LaminarInterface& laminar;
    kj::LowLevelAsyncIoProvider* asyncio;
    std::unordered_map<std::string, std::list<kj::PromiseFulfillerPair<void>>> locks;
    std::unordered_map<const Run*, std::list<kj::PromiseFulfillerPair<RunState>>> runWaiters;
};


// This is the implementation of the HTTP/Websocket interface. It exposes
// websocket connections as LaminarClients and registers them with the
// LaminarInterface so that status messages will be delivered to the client.
// On opening a websocket connection, it delivers a status snapshot message
// (see LaminarInterface::sendStatus)
class Server::HttpImpl {
public:
    HttpImpl(LaminarInterface& l) :
        laminar(l)
    {
        // debug logging
        // wss.set_access_channels(websocketpp::log::alevel::all);
        // wss.set_error_channels(websocketpp::log::elevel::all);

        // TODO: This could be used in the future to trigger actions on the
        // server in response to a web client request. Currently not supported.
        // wss.set_message_handler([](std::weak_ptr<void> s, websocket::message_ptr msg){
        //     msg->get_payload();
        // });

        // Handle plain HTTP requests by delivering the binary resource
        wss.set_http_handler([this](websocketpp::connection_hdl hdl){
            websocket::connection_ptr c = wss.get_con_from_hdl(hdl);
            const char* start, *end, *content_type;
            std::string resource = c->get_resource();
            if(resource.compare(0, strlen("/archive/"), "/archive/") == 0) {
                std::string file(resource.substr(strlen("/archive/")));
                std::string content;
                if(laminar.getArtefact(file, content)) {
                    c->set_status(websocketpp::http::status_code::ok);
                    c->append_header("Content-Transfer-Encoding", "binary");
                    c->set_body(content);
                } else {
                    c->set_status(websocketpp::http::status_code::not_found);
                }
            } else if(resources.handleRequest(resource, &start, &end, &content_type)) {
                c->set_status(websocketpp::http::status_code::ok);
                c->append_header("Content-Type", content_type);
                c->append_header("Content-Encoding", "gzip");
                c->append_header("Content-Transfer-Encoding", "binary");
                std::string response(start,end);
                c->set_body(response);
            } else {
                // 404
                c->set_status(websocketpp::http::status_code::not_found);
            }
            c->lc->close(false);
        });

        // Handle new websocket connection. Parse the URL to determine
        // the client's scope of interest, register the client for update
        // messages, and call sendStatus.
        wss.set_open_handler([this](websocketpp::connection_hdl hdl){
            websocket::connection_ptr c = wss.get_con_from_hdl(hdl);
            std::string res = c->get_resource();
            if(res.substr(0, 5) == "/jobs") {
                if(res.length() == 5) {
                    c->lc->scope.type = MonitorScope::ALL;
                } else {
                    res = res.substr(5);
                    int split = res.find('/',1);
                    std::string job = res.substr(1,split-1);
                    if(!job.empty()) {
                        c->lc->scope.job = job;
                        c->lc->scope.type = MonitorScope::JOB;
                    }
                    if(split != std::string::npos) {
                        int split2 = res.find('/', split+1);
                        std::string run = res.substr(split+1, split2-split);
                        if(!run.empty()) {
                            c->lc->scope.num = atoi(run.c_str());
                            c->lc->scope.type = MonitorScope::RUN;
                        }
                        if(split2 != std::string::npos && res.compare(split2, 4, "/log") == 0) {
                            c->lc->scope.type = MonitorScope::LOG;
                        }
                    }
                }
            }
            laminar.registerClient(c->lc);
            laminar.sendStatus(c->lc);
        });

        wss.set_close_handler([this](websocketpp::connection_hdl hdl){
            websocket::connection_ptr c = wss.get_con_from_hdl(hdl);
            laminar.deregisterClient(c->lc);
            c->lc->close();
        });
    }

    // Return a new connection object linked with the context defined below.
    // This is a bit untidy, it would be better to make them a single object,
    // but I didn't yet figure it out
    websocket::connection_ptr newConnection(LaminarClient* lc) {
        websocket::connection_ptr c = wss.get_connection();
        c->lc = lc;
        return c;
    }

private:
    Resources resources;
    LaminarInterface& laminar;
    websocket wss;
};

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

// Context for a WebsocketConnection (implements LaminarClient)
// This object is a streambuf and reimplements xsputn so that it can follow any
// write the websocketpp library makes to it with a write to the appropriate
// descriptor in the kj-async context.
struct Server::WebsocketConnection : public LaminarClient, public std::streambuf {
    WebsocketConnection(kj::Own<kj::AsyncIoStream>&& stream, Server::HttpImpl& http) :
        stream(kj::mv(stream)),
        out(this),
        cn(http.newConnection(this)),
        writePaf(kj::newPromiseAndFulfiller<void>()),
        closeOnComplete(false)
    {
        cn->register_ostream(&out);
        cn->start();
    }

    ~WebsocketConnection() noexcept(true) {
        outputBuffer.clear();
        writePaf.fulfiller->fulfill();
    }

    kj::Promise<void> pend() {
        return stream->tryRead(ibuf, 1, sizeof(ibuf)).then([this](size_t sz){
            cn->read_all(ibuf, sz);
            if(sz == 0 || cn->get_state() == websocketpp::session::state::closed) {
                cn->eof();
                return kj::Promise<void>(kj::READY_NOW);
            }
            return pend();
        });
    }

    kj::Promise<void> writeTask() {
        return writePaf.promise.then([this]() {
            std::string payload;
            // clear the outputBuffer for more context, and take a chunk
            // to send now
            payload.swap(outputBuffer);
            writePaf = kj::newPromiseAndFulfiller<void>();
            if(payload.empty()) {
                stream->shutdownWrite();
                return kj::Promise<void>(kj::READY_NOW);
            } else {
                return stream->write(payload.data(), payload.size()).then([this](){
                    return closeOnComplete ? stream->shutdownWrite(), kj::Promise<void>(kj::READY_NOW) : writeTask();
                }).attach(kj::mv(payload));
            }
        });
    }

    void sendMessage(std::string payload) override {
        cn->send(payload, websocketpp::frame::opcode::text);
    }

    void close(bool now) override {
        closeOnComplete = true;
        if(now) {
            outputBuffer.clear();
            writePaf.fulfiller->fulfill();
        }
    }

    std::streamsize xsputn(const char* s, std::streamsize sz) override {
        outputBuffer.append(std::string(s, sz));
        writePaf.fulfiller->fulfill();
        return sz;
    }

    kj::Own<kj::AsyncIoStream> stream;
    std::ostream out;
    websocket::connection_ptr cn;
    std::string outputBuffer;
    kj::PromiseFulfillerPair<void> writePaf;
    char ibuf[131072];
    bool closeOnComplete;
};

Server::Server(LaminarInterface& li, kj::StringPtr rpcBindAddress,
               kj::StringPtr httpBindAddress) :
    rpcInterface(kj::heap<RpcImpl>(li)),
    httpInterface(new HttpImpl(li)),
    ioContext(kj::setupAsyncIo()),
    tasks(*this)
{
    // RPC task
    if(rpcBindAddress.startsWith("unix:"))
        unlink(rpcBindAddress.slice(strlen("unix:")).cStr());
    tasks.add(ioContext.provider->getNetwork().parseAddress(rpcBindAddress)
              .then([this](kj::Own<kj::NetworkAddress>&& addr) {
        acceptRpcClient(addr->listen());
    }));

    // HTTP task
    if(httpBindAddress.startsWith("unix:"))
        unlink(httpBindAddress.slice(strlen("unix:")).cStr());
    tasks.add(ioContext.provider->getNetwork().parseAddress(httpBindAddress)
              .then([this](kj::Own<kj::NetworkAddress>&& addr) {
        acceptHttpClient(addr->listen());
    }));
}

Server::~Server() {
    // RpcImpl is deleted through Capability::Client.
    // Deal with the HTTP interface the old-fashioned way
    delete httpInterface;
}

void Server::start() {
    // this eventfd is just to allow us to quit the server at some point
    // in the future by adding this event to the async loop. I couldn't see
    // a simpler way...
    efd_quit = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
    kj::Promise<void> quit = kj::evalLater([this](){
        static uint64_t _;
        auto wakeEvent = ioContext.lowLevelProvider->wrapInputFd(efd_quit);
        return wakeEvent->read(&_, sizeof(uint64_t)).attach(std::move(wakeEvent));
    });
    quit.wait(ioContext.waitScope);
}

void Server::stop() {
    eventfd_write(efd_quit, 1);
}

void Server::addDescriptor(int fd, std::function<void(const char*,size_t)> cb) {
    auto event = this->ioContext.lowLevelProvider->wrapInputFd(fd, kj::LowLevelAsyncIoProvider::TAKE_OWNERSHIP);
    auto buffer = kj::heapArrayBuilder<char>(PROC_IO_BUFSIZE);
    tasks.add(handleFdRead(event, buffer.asPtr().begin(), cb).attach(std::move(event)).attach(std::move(buffer)));
}

void Server::acceptHttpClient(kj::Own<kj::ConnectionReceiver>&& listener) {
    auto ptr = listener.get();
    tasks.add(ptr->accept().then(kj::mvCapture(kj::mv(listener),
        [this](kj::Own<kj::ConnectionReceiver>&& listener,
                            kj::Own<kj::AsyncIoStream>&& connection) {
            acceptHttpClient(kj::mv(listener));
            auto conn = kj::heap<WebsocketConnection>(kj::mv(connection), *httpInterface);
            auto promises = kj::heapArrayBuilder<kj::Promise<void>>(2);
            promises.add(std::move(conn->pend()));
            promises.add(std::move(conn->writeTask()));
            return kj::joinPromises(promises.finish()).attach(std::move(conn));
        }))
    );
}

void Server::acceptRpcClient(kj::Own<kj::ConnectionReceiver>&& listener) {
    auto ptr = listener.get();
    tasks.add(ptr->accept().then(kj::mvCapture(kj::mv(listener),
        [this](kj::Own<kj::ConnectionReceiver>&& listener,
                            kj::Own<kj::AsyncIoStream>&& connection) {
            acceptRpcClient(kj::mv(listener));
            auto server = kj::heap<RpcConnection>(kj::mv(connection), rpcInterface, capnp::ReaderOptions());
            tasks.add(server->network.onDisconnect().attach(kj::mv(server)));
        }))
    );
}

// returns a promise which will read a chunk of data from the file descriptor
// wrapped by stream and invoke the provided callback with the read data.
// Repeats until ::read returns <= 0
kj::Promise<void> Server::handleFdRead(kj::AsyncInputStream* stream, char* buffer, std::function<void(const char*,size_t)> cb) {
    return stream->tryRead(buffer, 1, PROC_IO_BUFSIZE).then([this,stream,buffer,cb](size_t sz) {
        if(sz > 0) {
            cb(buffer, sz);
            return handleFdRead(stream, kj::mv(buffer), cb);
        }
        return kj::Promise<void>(kj::READY_NOW);
    });
}
