///
/// Copyright 2019-2020 Oliver Giles
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
#ifndef LAMINAR_HTTP_H_
#define LAMINAR_HTTP_H_

#include <kj/memory.h>
#include <kj/compat/http.h>
#include <string>
#include <set>

// Definition needed for musl
typedef unsigned int uint;
typedef unsigned long ulong;

class Laminar;
class Resources;
struct LogWatcher;
struct EventPeer;

class Http : public kj::HttpService {
public:
    Http(Laminar&li);
    virtual ~Http();

    kj::Promise<void> startServer(kj::Timer &timer, kj::Own<kj::ConnectionReceiver> &&listener);

    void notifyEvent(const char* data, std::string job = nullptr);
    void notifyLog(std::string job, uint run, std::string log_chunk, bool eot);

    // Allows supplying a custom HTML template. Pass an empty string to use the default.
    void setHtmlTemplate(std::string tmpl = std::string());

private:
    virtual kj::Promise<void> request(kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders& headers,
                                      kj::AsyncInputStream& requestBody, Response& response) override;
    bool parseLogEndpoint(kj::StringPtr url, std::string &name, uint &num);

    // With SSE, there is no notification if a client disappears. Also, an idle
    // client must be kept alive if there is no activity in their MonitorScope.
    // Deal with these by sending a periodic keepalive and reaping the client if
    // the write fails.
    kj::Promise<void> cleanupPeers(kj::Timer &timer);

    Laminar& laminar;
    std::set<EventPeer*> eventPeers;
    kj::Own<kj::HttpHeaderTable> headerTable;
    kj::Own<Resources> resources;
    std::set<LogWatcher*> logWatchers;

    kj::HttpHeaderId ACCEPT;
};

#endif //LAMINAR_HTTP_H_
