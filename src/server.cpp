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
#include "server.h"
#include "log.h"
#include "rpc.h"
#include "http.h"
#include "laminar.h"

#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/threadlocal.h>

#include <signal.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>

// Size of buffer used to read from file descriptors. Should be
// a multiple of sizeof(struct signalfd_siginfo) == 128
#define PROC_IO_BUFSIZE 4096

Server::Server(kj::AsyncIoContext& io) :
    ioContext(io),
    listeners(kj::heap<kj::TaskSet>(*this)),
    childTasks(*this)
{
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
    // 2. wait for all children to close
    childTasks.onEmpty().wait(ioContext.waitScope);
    // TODO not sure the comments below are true
    // 3. run the loop once more to send any pending output to http clients
    ioContext.waitScope.poll();
    // 4. return: http connections will be destructed when class is deleted
}

void Server::stop() {
    // This method is expected to be called in signal context, so an eventfd
    // is used to get the main loop to react. See run()
    eventfd_write(efd_quit, 1);
}

kj::Promise<void> Server::readDescriptor(int fd, std::function<void(const char*,size_t)> cb) {
    auto event = this->ioContext.lowLevelProvider->wrapInputFd(fd, kj::LowLevelAsyncIoProvider::TAKE_OWNERSHIP);
    auto buffer = kj::heapArrayBuilder<char>(PROC_IO_BUFSIZE);
    return handleFdRead(event, buffer.asPtr().begin(), cb).attach(std::move(event)).attach(std::move(buffer));
}

void Server::addTask(kj::Promise<void>&& task) {
    childTasks.add(kj::mv(task));
}

kj::Promise<void> Server::addTimeout(int seconds, std::function<void ()> cb) {
    return ioContext.lowLevelProvider->getTimer().afterDelay(seconds * kj::SECONDS).then([cb](){
        cb();
    }).eagerlyEvaluate(nullptr);
}

kj::Promise<int> Server::onChildExit(kj::Maybe<pid_t> &pid) {
    return ioContext.unixEventPort.onChildExit(pid);
}

Server::PathWatcher& Server::watchPaths(std::function<void()> fn)
{
    struct PathWatcherImpl final : public PathWatcher {
        PathWatcher& addPath(const char* path) override {
            inotify_add_watch(fd, path, IN_ONLYDIR | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE);
            return *this;
        }
        int fd;
    };
    auto pwi = kj::heap<PathWatcherImpl>();
    PathWatcher* pw = pwi.get();

    pwi->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    listeners->add(readDescriptor(pwi->fd, [fn](const char*, size_t){
        fn();
    }).attach(kj::mv(pwi)));
    return *pw;
}

void Server::listenRpc(Rpc &rpc, kj::StringPtr rpcBindAddress)
{
    if(rpcBindAddress.startsWith("unix:"))
        unlink(rpcBindAddress.slice(strlen("unix:")).cStr());
    listeners->add(ioContext.provider->getNetwork().parseAddress(rpcBindAddress)
              .then([this,&rpc](kj::Own<kj::NetworkAddress>&& addr) {
        return acceptRpcClient(rpc, addr->listen());
    }));

}

void Server::listenHttp(Http &http, kj::StringPtr httpBindAddress)
{
    if(httpBindAddress.startsWith("unix:"))
        unlink(httpBindAddress.slice(strlen("unix:")).cStr());
    listeners->add(ioContext.provider->getNetwork().parseAddress(httpBindAddress)
              .then([this,&http](kj::Own<kj::NetworkAddress>&& addr) {
        return http.startServer(ioContext.lowLevelProvider->getTimer(), addr->listen());
    }));
}

kj::Promise<void> Server::acceptRpcClient(Rpc& rpc, kj::Own<kj::ConnectionReceiver>&& listener) {
    kj::ConnectionReceiver& cr = *listener.get();
    return cr.accept().then(kj::mvCapture(kj::mv(listener),
        [this, &rpc](kj::Own<kj::ConnectionReceiver>&& listener, kj::Own<kj::AsyncIoStream>&& connection) {
            addTask(rpc.accept(kj::mv(connection)));
            return acceptRpcClient(rpc, kj::mv(listener));
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
    //kj::throwFatalException(kj::mv(exception));
    // prettier
    fprintf(stderr, "fatal: %s\n", exception.getDescription().cStr());
    exit(EXIT_FAILURE);
}
