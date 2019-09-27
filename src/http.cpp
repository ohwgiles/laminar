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
#include "interface.h"
#include "http.h"
#include "resources.h"
#include "log.h"

#include <kj/compat/http.h>
#include <rapidjson/document.h>

// This is the implementation of the HTTP/Websocket interface. It creates
// websocket connections as LaminarClients and registers them with the
// LaminarInterface so that status messages will be delivered to the client.
// On opening a websocket connection, it delivers a status snapshot message
// (see LaminarInterface::sendStatus)
class HttpImpl : public kj::HttpService {
public:
    HttpImpl(LaminarInterface& laminar, kj::HttpHeaderTable&tbl) :
        laminar(laminar),
        responseHeaders(tbl)
    {}
    virtual ~HttpImpl() {}

private:
    class HttpChunkedClient : public LaminarClient {
    public:
        HttpChunkedClient(LaminarInterface& laminar) :
            laminar(laminar)
        {}
        ~HttpChunkedClient() override {
            laminar.deregisterClient(this);
        }
        void sendMessage(std::string payload) override {
            chunks.push_back(kj::mv(payload));
            fulfiller->fulfill();
        }
        void notifyJobFinished() override {
            done = true;
            fulfiller->fulfill();
        }
        LaminarInterface& laminar;
        std::list<std::string> chunks;
        // cannot use chunks.empty() because multiple fulfill()s
        // could be coalesced
        bool done = false;
        kj::Own<kj::PromiseFulfiller<void>> fulfiller;
    };

    // Implements LaminarClient and holds the Websocket connection object.
    // Automatically destructed when the promise created in request() resolves
    // or is cancelled
    class WebsocketClient : public LaminarClient {
    public:
        WebsocketClient(LaminarInterface& laminar, kj::Own<kj::WebSocket>&& ws) :
            laminar(laminar),
            ws(kj::mv(ws))
        {}
        ~WebsocketClient() override {
            laminar.deregisterClient(this);
        }
        virtual void sendMessage(std::string payload) override {
            messages.emplace_back(kj::mv(payload));
            // sendMessage might be called several times before the event loop
            // gets a chance to act on the fulfiller. So store the payload here
            // where it can be fetched later and don't pass the payload with the
            // fulfiller because subsequent calls to fulfill() are ignored.
            fulfiller->fulfill();
        }
        LaminarInterface& laminar;
        kj::Own<kj::WebSocket> ws;
        std::list<std::string> messages;
        kj::Own<kj::PromiseFulfiller<void>> fulfiller;
    };

    kj::Promise<void> websocketRead(WebsocketClient& lc)
    {
        return lc.ws->receive().then([&lc,this](kj::WebSocket::Message&& message) {
            KJ_SWITCH_ONEOF(message) {
                KJ_CASE_ONEOF(str, kj::String) {
                    rapidjson::Document d;
                    d.ParseInsitu(const_cast<char*>(str.cStr()));
                    if(d.HasMember("page") && d["page"].IsInt() && d.HasMember("field") && d["field"].IsString() && d.HasMember("order") && d["order"].IsString()) {
                        lc.scope.page = d["page"].GetInt();
                        lc.scope.field = d["field"].GetString();
                        lc.scope.order_desc = strcmp(d["order"].GetString(), "dsc") == 0;
                        laminar.sendStatus(&lc);
                        return websocketRead(lc);
                    }
                }
                KJ_CASE_ONEOF(close, kj::WebSocket::Close) {
                    // clean socket shutdown
                    return lc.ws->close(close.code, close.reason);
                }
                KJ_CASE_ONEOF_DEFAULT {}
            }
            // unhandled/unknown message
            return lc.ws->disconnect();
        }, [](kj::Exception&& e){
            // server logs suggest early catching here avoids fatal exception later
            // TODO: reproduce in unit test
            LLOG(WARNING, e.getDescription());
            return kj::READY_NOW;
        });
    }

    kj::Promise<void> websocketWrite(WebsocketClient& lc)
    {
        auto paf = kj::newPromiseAndFulfiller<void>();
        lc.fulfiller = kj::mv(paf.fulfiller);
        return paf.promise.then([this,&lc]{
            kj::Promise<void> p = kj::READY_NOW;
            std::list<std::string> messages = kj::mv(lc.messages);
            for(std::string& s : messages) {
                p = p.then([&s,&lc]{
                    kj::String str = kj::str(s);
                    return lc.ws->send(str).attach(kj::mv(str));
                });
            }
            return p.attach(kj::mv(messages)).then([this,&lc]{
                return websocketWrite(lc);
            });
        });
    }

    kj::Promise<void> websocketUpgraded(WebsocketClient& lc, std::string resource) {
        // convert the requested URL to a MonitorScope
        if(resource.substr(0, 5) == "/jobs") {
            if(resource.length() == 5) {
                lc.scope.type = MonitorScope::ALL;
            } else {
                resource = resource.substr(5);
                size_t split = resource.find('/',1);
                std::string job = resource.substr(1,split-1);
                if(!job.empty()) {
                    lc.scope.job = job;
                    lc.scope.type = MonitorScope::JOB;
                }
                if(split != std::string::npos) {
                    size_t split2 = resource.find('/', split+1);
                    std::string run = resource.substr(split+1, split2-split);
                    if(!run.empty()) {
                        lc.scope.num = static_cast<uint>(atoi(run.c_str()));
                        lc.scope.type = MonitorScope::RUN;
                    }
                    if(split2 != std::string::npos && resource.compare(split2, 4, "/log") == 0) {
                        lc.scope.type = MonitorScope::LOG;
                    }
                }
            }
        }
        laminar.registerClient(&lc);
        kj::Promise<void> connection = websocketRead(lc).exclusiveJoin(websocketWrite(lc));
        // registerClient can happen after a successful websocket handshake.
        // However, the connection might not be closed gracefully, so the
        // corresponding deregister operation happens in the WebsocketClient
        // destructor rather than the close handler or a then() clause
        laminar.sendStatus(&lc);
        return connection;
    }

    // Parses the url of the form /log/NAME/NUMBER, filling in the passed
    // references and returning true if successful. /log/NAME/latest is
    // also allowed, in which case the num reference is set to 0
    bool parseLogEndpoint(kj::StringPtr url, std::string& name, uint& num) {
        if(url.startsWith("/log/")) {
            kj::StringPtr path = url.slice(5);
            KJ_IF_MAYBE(sep, path.findFirst('/')) {
                name = path.slice(0, *sep).begin();
                kj::StringPtr tail = path.slice(*sep+1);
                num = static_cast<uint>(atoi(tail.begin()));
                name.erase(*sep);
                if(tail == "latest")
                    num = laminar.latestRun(name);
                if(num > 0)
                    return true;
            }
        }
        return false;
    }

    kj::Promise<void> writeLogChunk(HttpChunkedClient* client, kj::AsyncOutputStream* stream) {
        auto paf = kj::newPromiseAndFulfiller<void>();
        client->fulfiller = kj::mv(paf.fulfiller);
        return paf.promise.then([=]{
            kj::Promise<void> p = kj::READY_NOW;
            std::list<std::string> chunks = kj::mv(client->chunks);
            for(std::string& s : chunks) {
                p = p.then([=,&s]{
                    return stream->write(s.data(), s.size());
                });
            }
            return p.attach(kj::mv(chunks)).then([=]{
                return client->done ? kj::Promise<void>(kj::READY_NOW) : writeLogChunk(client, stream);
            });
        });
    }

    virtual kj::Promise<void> request(kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders& headers,
            kj::AsyncInputStream& requestBody, Response& response) override
    {
        if(headers.isWebSocket()) {
            responseHeaders.clear();
            kj::Own<WebsocketClient> lc = kj::heap<WebsocketClient>(laminar, response.acceptWebSocket(responseHeaders));
            return websocketUpgraded(*lc, url.cStr()).attach(kj::mv(lc));
        } else {
            // handle regular HTTP request
            const char* start, *end, *content_type;
            std::string badge;
            // for log requests
            std::string name;
            uint num;
            responseHeaders.clear();
            // Clients usually expect that http servers will ignore unknown query parameters,
            // and expect to use this feature to work around browser limitations like there
            // being no way to programatically force a resource to be reloaded from the server
            // (without "Cache-Control: no-store", which is overkill). See issue #89.
            // Since we currently don't handle any query parameters at all, the easiest way
            // to achieve this is unconditionally remove all query parameters from the request.
            // This will need to be redone if we ever accept query parameters, which probably
            // will happen as part of issue #90.
            KJ_IF_MAYBE(queryIdx, url.findFirst('?')) {
                const_cast<char*>(url.begin())[*queryIdx] = '\0';
                url = url.begin();
            }
            if(url.startsWith("/archive/")) {
                KJ_IF_MAYBE(file, laminar.getArtefact(url.slice(strlen("/archive/")))) {
                    auto array = (*file)->mmap(0, (*file)->stat().size);
                    responseHeaders.add("Content-Transfer-Encoding", "binary");
                    auto stream = response.send(200, "OK", responseHeaders, array.size());
                    return stream->write(array.begin(), array.size()).attach(kj::mv(array)).attach(kj::mv(file)).attach(kj::mv(stream));
                }
            } else if(parseLogEndpoint(url, name, num)) {
                kj::Own<HttpChunkedClient> cc = kj::heap<HttpChunkedClient>(laminar);
                cc->scope.job = name;
                cc->scope.num = num;
                bool complete;
                std::string output;
                cc->scope.type = MonitorScope::LOG;
                if(laminar.handleLogRequest(name, num, output, complete)) {
                    responseHeaders.set(kj::HttpHeaderId::CONTENT_TYPE, "text/plain; charset=utf-8");
                    responseHeaders.add("Content-Transfer-Encoding", "binary");
                    // Disables nginx reverse-proxy's buffering. Necessary for dynamic log output.
                    responseHeaders.add("X-Accel-Buffering", "no");
                    auto stream = response.send(200, "OK", responseHeaders, nullptr);
                    laminar.registerClient(cc.get());
                    return stream->write(output.data(), output.size()).then([=,s=stream.get(),c=cc.get()]{
                        if(complete)
                            return kj::Promise<void>(kj::READY_NOW);
                        return writeLogChunk(c, s);
                    }).attach(kj::mv(output)).attach(kj::mv(stream)).attach(kj::mv(cc));
                }
            } else if(url == "/custom/style.css") {
                responseHeaders.set(kj::HttpHeaderId::CONTENT_TYPE, "text/css; charset=utf-8");
                responseHeaders.add("Content-Transfer-Encoding", "binary");
                std::string css = laminar.getCustomCss();
                auto stream = response.send(200, "OK", responseHeaders, css.size());
                return stream->write(css.data(), css.size()).attach(kj::mv(css)).attach(kj::mv(stream));
            } else if(resources.handleRequest(url.cStr(), &start, &end, &content_type)) {
                responseHeaders.set(kj::HttpHeaderId::CONTENT_TYPE, content_type);
                responseHeaders.add("Content-Encoding", "gzip");
                responseHeaders.add("Content-Transfer-Encoding", "binary");
                auto stream = response.send(200, "OK", responseHeaders, end-start);
                return stream->write(start, end-start).attach(kj::mv(stream));
            } else if(url.startsWith("/badge/") && url.endsWith(".svg") && laminar.handleBadgeRequest(std::string(url.begin()+7, url.size()-11), badge)) {
                responseHeaders.set(kj::HttpHeaderId::CONTENT_TYPE, "image/svg+xml");
                responseHeaders.add("Cache-Control", "no-cache");
                auto stream = response.send(200, "OK", responseHeaders, badge.size());
                return stream->write(badge.data(), badge.size()).attach(kj::mv(badge)).attach(kj::mv(stream));
            }
            return response.sendError(404, "Not Found", responseHeaders);
        }
    }

    LaminarInterface& laminar;
    Resources resources;
    kj::HttpHeaders responseHeaders;
};

Http::Http(LaminarInterface &li) :
    headerTable(kj::heap<kj::HttpHeaderTable>()),
    httpService(kj::heap<HttpImpl>(li, *headerTable)),
    laminar(li)
{

}

kj::Promise<void> Http::startServer(kj::Timer& timer, kj::Own<kj::ConnectionReceiver>&& listener) {
    auto httpServer = kj::heap<kj::HttpServer>(timer, *headerTable, *httpService);
    return httpServer->listenHttp(*listener).attach(kj::mv(listener)).attach(kj::mv(httpServer));
}
