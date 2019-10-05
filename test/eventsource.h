///
/// Copyright 2019 Oliver Giles
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
#ifndef LAMINAR_EVENTSOURCE_H_
#define LAMINAR_EVENTSOURCE_H_

#include <kj/async-io.h>
#include <kj/compat/http.h>
#include <rapidjson/document.h>
#include <vector>

class EventSource {
public:
    EventSource(kj::AsyncIoContext& ctx, const char* httpConnectAddr, const char* path) :
        networkAddress(ctx.provider->getNetwork().parseAddress(httpConnectAddr).wait(ctx.waitScope)),
        httpClient(kj::newHttpClient(ctx.lowLevelProvider->getTimer(), headerTable, *networkAddress)),
        headerTable(),
        headers(headerTable),
        buffer(kj::heapArrayBuilder<char>(BUFFER_SIZE))
    {
        headers.add("Accept", "text/event-stream");
        auto resp = httpClient->request(kj::HttpMethod::GET, path, headers).response.wait(ctx.waitScope);
        promise = waitForMessages(resp.body.get(), 0).attach(kj::mv(resp));
    }

    const std::vector<rapidjson::Document>& messages() {
        return receivedMessages;
    }

private:
    kj::Own<kj::NetworkAddress> networkAddress;
    kj::Own<kj::HttpClient> httpClient;
    kj::HttpHeaderTable headerTable;
    kj::HttpHeaders headers;
    kj::ArrayBuilder<char> buffer;
    kj::Maybe<kj::Promise<void>> promise;
    std::vector<rapidjson::Document> receivedMessages;

    kj::Promise<void> waitForMessages(kj::AsyncInputStream* stream, ulong offset) {
        return stream->read(buffer.asPtr().begin() + offset, 1, BUFFER_SIZE).then([=](size_t s) {
            ulong end = offset + s;
            buffer.asPtr().begin()[end] = '\0';
            if(strcmp(&buffer.asPtr().begin()[end - 2], "\n\n") == 0) {
                rapidjson::Document d;
                d.Parse(buffer.begin() + strlen("data: "));
                receivedMessages.emplace_back(kj::mv(d));
                end = 0;
            }
            return waitForMessages(stream, end);
        });
    }

    static const int BUFFER_SIZE = 1024;
};

#endif // LAMINAR_EVENTSOURCE_H_
