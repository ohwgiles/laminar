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
#include "http.h"
#include "resources.h"
#include "monitorscope.h"
#include "log.h"

#include "laminar.h"

// Helper class which wraps another class with calls to
// adding and removing a pointer to itself from a passed
// std::set reference. Used to keep track of currently
// connected clients
template<typename T, typename ...Args>
struct WithSetRef : public T {
    WithSetRef(std::set<T*>& set, Args&& ...args) :
        T(std::forward(args)...),
        _set(set)
    {
        _set.insert(this);
    }
    ~WithSetRef() {
        _set.erase(this);
    }
private:
    std::set<T*>& _set;
};

struct EventPeer {
    MonitorScope scope;
    std::list<std::string> pendingOutput;
    kj::Own<kj::PromiseFulfiller<void>> fulfiller;
};

struct LogWatcher {
    std::string job;
    uint run;
    std::list<std::string> pendingOutput;
    kj::Own<kj::PromiseFulfiller<bool>> fulfiller;
};

kj::Maybe<MonitorScope> fromUrl(std::string resource, char* query) {
    MonitorScope scope;

    if(query) {
        char *sk;
        for(char* k = strtok_r(query, "&", &sk); k; k = strtok_r(nullptr, "&", &sk)) {
            if(char* v = strchr(k, '=')) {
                *v++ = '\0';
                if(strcmp(k, "page") == 0)
                    scope.page = atoi(v);
                else if(strcmp(k, "field") == 0)
                    scope.field = v;
                else if(strcmp(k, "order") == 0)
                    scope.order_desc = (strcmp(v, "dsc") == 0);
            }
        }
    }

    if(resource == "/") {
        scope.type = MonitorScope::HOME;
        return kj::mv(scope);
    }

    if(resource == "/jobs" || resource == "/wallboard") {
        scope.type = MonitorScope::ALL;
        return kj::mv(scope);
    }

    if(resource.substr(0, 5) != "/jobs")
        return nullptr;

    resource = resource.substr(5);
    size_t split = resource.find('/',1);
    std::string job = resource.substr(1,split-1);
    if(job.empty())
        return nullptr;

    scope.job = job;
    scope.type = MonitorScope::JOB;
    if(split == std::string::npos)
        return kj::mv(scope);

    size_t split2 = resource.find('/', split+1);
    std::string run = resource.substr(split+1, split2-split);
    if(run.empty())
        return nullptr;

    scope.num = static_cast<uint>(atoi(run.c_str()));
    scope.type = MonitorScope::RUN;
    return kj::mv(scope);
}

// Parses the url of the form /log/NAME/NUMBER, filling in the passed
// references and returning true if successful. /log/NAME/latest is
// also allowed, in which case the num reference is set to 0
bool Http::parseLogEndpoint(kj::StringPtr url, std::string& name, uint& num) {
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

kj::Promise<void> Http::cleanupPeers(kj::Timer& timer)
{
    return timer.afterDelay(15 * kj::SECONDS).then([&]{
        for(EventPeer* p : eventPeers) {
            // an empty SSE message is a colon followed by two newlines
            p->pendingOutput.push_back(":\n\n");
            p->fulfiller->fulfill();
        }
        return cleanupPeers(timer);
    }).eagerlyEvaluate(nullptr);
}

kj::Promise<void> writeEvents(EventPeer* peer, kj::AsyncOutputStream* stream) {
    auto paf = kj::newPromiseAndFulfiller<void>();
    peer->fulfiller = kj::mv(paf.fulfiller);
    return paf.promise.then([=]{
        kj::Promise<void> p = kj::READY_NOW;
        std::list<std::string> chunks = kj::mv(peer->pendingOutput);
        for(std::string& s : chunks) {
            p = p.then([=,&s]{
                return stream->write(s.data(), s.size());
            });
        }
        return p.attach(kj::mv(chunks)).then([=]{
            return writeEvents(peer, stream);
        });
    });
}

kj::Promise<void> writeLogChunk(LogWatcher* client, kj::AsyncOutputStream* stream) {
    auto paf = kj::newPromiseAndFulfiller<bool>();
    client->fulfiller = kj::mv(paf.fulfiller);
    return paf.promise.then([=](bool done){
        kj::Promise<void> p = kj::READY_NOW;
        std::list<std::string> chunks = kj::mv(client->pendingOutput);
        for(std::string& s : chunks) {
            p = p.then([=,&s]{
                return stream->write(s.data(), s.size());
            });
        }
        return p.attach(kj::mv(chunks)).then([=]{
            return done ? kj::Promise<void>(kj::READY_NOW) : writeLogChunk(client, stream);
        });
    });
}

kj::Promise<void> Http::request(kj::HttpMethod method, kj::StringPtr url, const kj::HttpHeaders &headers, kj::AsyncInputStream &requestBody, HttpService::Response &response)
{
    const char* start, *end, *content_type;
    std::string badge;
    // for log requests
    std::string name;
    uint num;
    kj::HttpHeaders responseHeaders(*headerTable);
    responseHeaders.clear();
    bool is_sse = false;
    char* queryString = nullptr;
    // Clients usually expect that http servers will ignore unknown query parameters,
    // and expect to use this feature to work around browser limitations like there
    // being no way to programatically force a resource to be reloaded from the server
    // (without "Cache-Control: no-store", which is overkill). See issue #89.
    // So first parse any query parameters we *are* interested in, then simply remove
    // them from the URL, to make comparisions easier.
    KJ_IF_MAYBE(queryIdx, url.findFirst('?')) {
        const_cast<char*>(url.begin())[*queryIdx] = '\0';
        queryString = const_cast<char*>(url.begin() + *queryIdx + 1);
        url = url.begin();
    }

    KJ_IF_MAYBE(accept, headers.get(ACCEPT)) {
        is_sse = (*accept == "text/event-stream");
    }

    if(is_sse) {
        KJ_IF_MAYBE(s, fromUrl(url.cStr(), queryString)) {
            responseHeaders.set(kj::HttpHeaderId::CONTENT_TYPE, "text/event-stream");
            // Disables nginx reverse-proxy's buffering. Necessary for streamed events.
            responseHeaders.add("X-Accel-Buffering", "no");
            auto peer = kj::heap<WithSetRef<EventPeer>>(eventPeers);
            peer->scope = *s;
            std::string st = "data: " + laminar.getStatus(peer->scope) + "\n\n";
            auto stream = response.send(200, "OK", responseHeaders);
            return stream->write(st.data(), st.size()).attach(kj::mv(st)).then([=,s=stream.get(),p=peer.get()]{
                return writeEvents(p,s);
            }).attach(kj::mv(stream)).attach(kj::mv(peer));
        }
    } else if(url.startsWith("/archive/")) {
        KJ_IF_MAYBE(file, laminar.getArtefact(url.slice(strlen("/archive/")))) {
            auto array = (*file)->mmap(0, (*file)->stat().size);
            responseHeaders.add("Content-Transfer-Encoding", "binary");
            auto stream = response.send(200, "OK", responseHeaders, array.size());
            return stream->write(array.begin(), array.size()).attach(kj::mv(array)).attach(kj::mv(file)).attach(kj::mv(stream));
        }
    } else if(parseLogEndpoint(url, name, num)) {
        bool complete;
        std::string output;
        if(laminar.handleLogRequest(name, num, output, complete)) {
            responseHeaders.set(kj::HttpHeaderId::CONTENT_TYPE, "text/plain; charset=utf-8");
            responseHeaders.add("Content-Transfer-Encoding", "binary");
            // Disables nginx reverse-proxy's buffering. Necessary for dynamic log output.
            responseHeaders.add("X-Accel-Buffering", "no");
            auto stream = response.send(200, "OK", responseHeaders, nullptr);
            auto s = stream.get();
            auto lw = kj::heap<WithSetRef<LogWatcher>>(logWatchers);
            lw->job = name;
            lw->run = num;
            auto promise = writeLogChunk(lw.get(), stream.get()).attach(kj::mv(stream)).attach(kj::mv(lw));
            return s->write(output.data(), output.size()).attach(kj::mv(output)).then([p=kj::mv(promise),complete]() mutable {
                if(complete)
                    return kj::Promise<void>(kj::READY_NOW);
                return kj::mv(p);
            });
        }
    } else if(resources->handleRequest(url.cStr(), &start, &end, &content_type)) {
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

Http::Http(Laminar &li) :
  laminar(li),
  resources(kj::heap<Resources>())
{
    kj::HttpHeaderTable::Builder builder;
    ACCEPT = builder.add("Accept");
    headerTable = builder.build();
}

Http::~Http()
{
    LASSERT(logWatchers.size() == 0);
    LASSERT(eventPeers.size() == 0);
}

kj::Promise<void> Http::startServer(kj::Timer& timer, kj::Own<kj::ConnectionReceiver>&& listener)
{
    kj::Own<kj::HttpServer> server = kj::heap<kj::HttpServer>(timer, *headerTable, *this);
    return server->listenHttp(*listener).attach(cleanupPeers(timer)).attach(kj::mv(listener)).attach(kj::mv(server));
}

void Http::notifyEvent(const char *data, std::string job)
{
    for(EventPeer* c : eventPeers) {
        if(c->scope.wantsStatus(job)) {
            c->pendingOutput.push_back("data: " + std::string(data) + "\n\n");
            c->fulfiller->fulfill();
        }
    }
}

void Http::notifyLog(std::string job, uint run, std::string log_chunk, bool eot)
{
    for(LogWatcher* lw : logWatchers) {
        if(lw->job == job && lw->run == run) {
            lw->pendingOutput.push_back(log_chunk);
            lw->fulfiller->fulfill(kj::mv(eot));
        }
    }
}

void Http::setHtmlTemplate(std::string tmpl)
{
    resources->setHtmlTemplate(tmpl);
}
