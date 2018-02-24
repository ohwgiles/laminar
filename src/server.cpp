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
#include <sys/signal.h>
#include <sys/signalfd.h>

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

    ~RpcImpl() override {
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
        uint buildNum = context.getParams().getBuildNum();
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
            } else if(resource.compare("/custom/style.css") == 0) {
                c->set_status(websocketpp::http::status_code::ok);
                c->append_header("Content-Transfer-Encoding", "binary");
                c->set_body(laminar.getCustomCss());
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
                    size_t split = res.find('/',1);
                    std::string job = res.substr(1,split-1);
                    if(!job.empty()) {
                        c->lc->scope.job = job;
                        c->lc->scope.type = MonitorScope::JOB;
                    }
                    if(split != std::string::npos) {
                        size_t split2 = res.find('/', split+1);
                        std::string run = res.substr(split+1, split2-split);
                        if(!run.empty()) {
                            c->lc->scope.num = static_cast<uint>(atoi(run.c_str()));
                            c->lc->scope.type = MonitorScope::RUN;
                        }
                        if(split2 != std::string::npos && res.compare(split2, 4, "/log") == 0) {
                            c->lc->scope.type = MonitorScope::LOG;
                        }
                    }
                }
            }
            // registerClient can happen after a successful websocket handshake.
            // However, the connection might not be closed gracefully, so the
            // corresponding deregister operation happens in the connection
            // destructor rather than the close handler
            laminar.registerClient(c->lc);
            laminar.sendStatus(c->lc);
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

    void connectionDestroyed(LaminarClient* lc) {
        // This will be called for all connections, not just websockets, so
        // the laminar instance should silently ignore unknown clients
        laminar.deregisterClient(lc);
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
// This object maps read and write handlers between the websocketpp library
// and the corresponding kj async methods
struct Server::WebsocketConnection : public LaminarClient {
    WebsocketConnection(kj::Own<kj::AsyncIoStream>&& stream, Server::HttpImpl& http) :
        http(http),
        stream(kj::mv(stream)),
        cn(http.newConnection(this)),
        writePaf(kj::newPromiseAndFulfiller<void>())
    {
        cn->set_write_handler([this](websocketpp::connection_hdl, const char* s, size_t sz) {
            outputBuffer.append(std::string(s, sz));
            writePaf.fulfiller->fulfill();
            return std::error_code();
        });
        cn->start();
    }

    virtual ~WebsocketConnection() noexcept(true) override {
        // Removes the connection from the list of registered clients. Must be
        // here rather than in the websocket closing handshake because connections
        // might be unexpectedly/aggressively closed and any references must be
        // removed.
        http.connectionDestroyed(this);
    }

    kj::Promise<void> pend() {
        return stream->tryRead(ibuf, 1, sizeof(ibuf)).then([this](size_t sz){
            cn->read_some(ibuf, sz);
            if(sz == 0 || cn->get_state() == websocketpp::session::state::closed) {
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
            KJ_ASSERT(!payload.empty());
            return stream->write(payload.data(), payload.size()).then([this](){
                if(cn->get_state() == websocketpp::session::state::closed) {
                    return kj::Promise<void>(kj::READY_NOW);
                }
                return writeTask();
            }).attach(kj::mv(payload));
        });
    }

    void sendMessage(std::string payload) override {
        cn->send(payload, websocketpp::frame::opcode::text);
    }

    HttpImpl& http;
    kj::Own<kj::AsyncIoStream> stream;
    websocket::connection_ptr cn;
    std::string outputBuffer;
    kj::PromiseFulfillerPair<void> writePaf;
    char ibuf[131072];
};

Server::Server(LaminarInterface& li, kj::StringPtr rpcBindAddress,
               kj::StringPtr httpBindAddress) :
    rpcInterface(kj::heap<RpcImpl>(li)),
    laminarInterface(li),
    httpInterface(kj::heap<HttpImpl>(li)),
    ioContext(kj::setupAsyncIo()),
    listeners(kj::heap<kj::TaskSet>(*this)),
    childTasks(*this),
    httpConnections(*this),
    httpReady(kj::newPromiseAndFulfiller<void>())
{
    // RPC task
    if(rpcBindAddress.startsWith("unix:"))
        unlink(rpcBindAddress.slice(strlen("unix:")).cStr());
    listeners->add(ioContext.provider->getNetwork().parseAddress(rpcBindAddress)
              .then([this](kj::Own<kj::NetworkAddress>&& addr) {
        return acceptRpcClient(addr->listen());
    }));

    // HTTP task
    if(httpBindAddress.startsWith("unix:"))
        unlink(httpBindAddress.slice(strlen("unix:")).cStr());
    listeners->add(ioContext.provider->getNetwork().parseAddress(httpBindAddress)
              .then([this](kj::Own<kj::NetworkAddress>&& addr) {
        // TODO: a better way? Currently used only for testing
        httpReady.fulfiller->fulfill();
        return acceptHttpClient(addr->listen());
    }));

    // handle SIGCHLD
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    int sigchld = signalfd(-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
    auto event = ioContext.lowLevelProvider->wrapInputFd(sigchld, kj::LowLevelAsyncIoProvider::TAKE_OWNERSHIP);
    auto buffer = kj::heapArrayBuilder<char>(PROC_IO_BUFSIZE);
    reapWatch = handleFdRead(event, buffer.asPtr().begin(), [this](const char* buf, size_t){
        const struct signalfd_siginfo* siginfo = reinterpret_cast<const struct signalfd_siginfo*>(buf);
        KJ_ASSERT(siginfo->ssi_signo == SIGCHLD);
        laminarInterface.reapChildren();
    }).attach(std::move(event)).attach(std::move(buffer));
}

Server::~Server() {
}

void Server::start() {
    // The eventfd is used to quit the server later since we need to trigger
    // a reaction from the event loop
    efd_quit = eventfd(0, EFD_CLOEXEC|EFD_NONBLOCK);
    kj::evalLater([this](){
        static uint64_t _;
        auto wakeEvent = ioContext.lowLevelProvider->wrapInputFd(efd_quit);
        return wakeEvent->read(&_, sizeof(uint64_t)).attach(std::move(wakeEvent));
    }).wait(ioContext.waitScope);
    // Execution arrives here when the eventfd is triggered (in stop())

    // Shutdown sequence:
    // 1. stop accepting new connections
    listeners = nullptr;
    // 2. abort current jobs. Most of the time this isn't necessary since
    // systemd stop or other kill mechanism will send SIGTERM to the whole
    // process group.
    laminarInterface.abortAll();
    // 3. wait for all children to close
    childTasks.onEmpty().wait(ioContext.waitScope);
    // 4. run the loop once more to send any pending output to websocket clients
    ioContext.waitScope.poll();
    // 5. return: websockets will be destructed
}

void Server::stop() {
    // This method is expected to be called in signal context, so an eventfd
    // is used to get the main loop to react. See run()
    eventfd_write(efd_quit, 1);
}

void Server::addDescriptor(int fd, std::function<void(const char*,size_t)> cb) {
    auto event = this->ioContext.lowLevelProvider->wrapInputFd(fd, kj::LowLevelAsyncIoProvider::TAKE_OWNERSHIP);
    auto buffer = kj::heapArrayBuilder<char>(PROC_IO_BUFSIZE);
    childTasks.add(handleFdRead(event, buffer.asPtr().begin(), cb).attach(std::move(event)).attach(std::move(buffer)));
}

kj::Promise<void> Server::acceptHttpClient(kj::Own<kj::ConnectionReceiver>&& listener) {
    kj::ConnectionReceiver& cr = *listener.get();
    return cr.accept().then(kj::mvCapture(kj::mv(listener),
        [this](kj::Own<kj::ConnectionReceiver>&& listener, kj::Own<kj::AsyncIoStream>&& connection) {
            auto conn = kj::heap<WebsocketConnection>(kj::mv(connection), *httpInterface);
            // delete the connection when either the read or write task completes
            httpConnections.add(conn->pend().exclusiveJoin(conn->writeTask()).attach(kj::mv(conn)));
            return acceptHttpClient(kj::mv(listener));
        }));
}

kj::Promise<void> Server::acceptRpcClient(kj::Own<kj::ConnectionReceiver>&& listener) {
    kj::ConnectionReceiver& cr = *listener.get();
    return cr.accept().then(kj::mvCapture(kj::mv(listener),
        [this](kj::Own<kj::ConnectionReceiver>&& listener, kj::Own<kj::AsyncIoStream>&& connection) {
            auto server = kj::heap<RpcConnection>(kj::mv(connection), rpcInterface, capnp::ReaderOptions());
            childTasks.add(server->network.onDisconnect().attach(kj::mv(server)));
            return acceptRpcClient(kj::mv(listener));
        }));
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

void Server::taskFailed(kj::Exception &&exception) {
    // An unexpected http connection close can cause an exception, so don't re-throw.
    // TODO: consider re-throwing selected exceptions
    LLOG(INFO, exception);
    //kj::throwFatalException(kj::mv(exception));
}
