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
#ifndef LAMINAR_SERVER_H_
#define LAMINAR_SERVER_H_

#include <kj/async-io.h>
#include <kj/compat/http.h>
#include <capnp/message.h>
#include <capnp/capability.h>
#include <functional>

struct LaminarInterface;

// This class abstracts the HTTP/Websockets and Cap'n Proto RPC interfaces
// and manages the program's asynchronous event loop
class Server final : public kj::TaskSet::ErrorHandler {
public:
    // Initializes the server with a LaminarInterface to handle requests from
    // HTTP/Websocket or RPC clients and bind addresses for each of those
    // interfaces. See the documentation for kj::AsyncIoProvider::getNetwork
    // for a description of the address format
    Server(LaminarInterface& li, kj::StringPtr rpcBindAddress, kj::StringPtr httpBindAddress);
    ~Server();
    void start();
    void stop();

    // add a file descriptor to be monitored for output. The callback will be
    // invoked with the read data
    kj::Promise<void> readDescriptor(int fd, std::function<void(const char*,size_t)> cb);

    void addTask(kj::Promise<void> &&task);
    // add a one-shot timer callback
    kj::Promise<void> addTimeout(int seconds, std::function<void()> cb);

    // get a promise which resolves when a child process exits
    kj::Promise<int> onChildExit(kj::Maybe<pid_t>& pid);
    // add a path to be watched for changes
    void addWatchPath(const char* dpath);

private:
    kj::Promise<void> acceptRpcClient(kj::Own<kj::ConnectionReceiver>&& listener);
    kj::Promise<void> handleFdRead(kj::AsyncInputStream* stream, char* buffer, std::function<void(const char*,size_t)> cb);

    void taskFailed(kj::Exception&& exception) override;

private:
    int efd_quit;
    capnp::Capability::Client rpcInterface;
    LaminarInterface& laminarInterface;
    kj::AsyncIoContext ioContext;
    kj::HttpHeaderTable headerTable;
    kj::Own<kj::HttpService> httpService;
    kj::Own<kj::HttpServer> httpServer;
    kj::Own<kj::TaskSet> listeners;
    kj::TaskSet childTasks;
    kj::TaskSet httpConnections;
    kj::Maybe<kj::Promise<void>> reapWatch;
    int inotify_fd;
    kj::Maybe<kj::Promise<void>> pathWatch;

    // TODO: restructure so this isn't necessary
    friend class ServerTest;
    kj::PromiseFulfillerPair<void> httpReady;
};

#endif // LAMINAR_SERVER_H_
