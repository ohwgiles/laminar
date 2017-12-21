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
    void addDescriptor(int fd, std::function<void(const char*,size_t)> cb);

private:
    void acceptHttpClient(kj::Own<kj::ConnectionReceiver>&& listener);
    void acceptRpcClient(kj::Own<kj::ConnectionReceiver>&& listener);
    kj::Promise<void> handleFdRead(kj::AsyncInputStream* stream, char* buffer, std::function<void(const char*,size_t)> cb);

    void taskFailed(kj::Exception&& exception) override {
        kj::throwFatalException(kj::mv(exception));
    }

private:
    struct WebsocketConnection;
    class HttpImpl;

    int efd_quit;
    capnp::Capability::Client rpcInterface;
    HttpImpl* httpInterface;
    kj::AsyncIoContext ioContext;
    kj::TaskSet tasks;
};

#endif // LAMINAR_SERVER_H_
