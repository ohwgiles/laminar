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
#ifndef LAMINAR_FIXTURE_H_
#define LAMINAR_FIXTURE_H_

#include <capnp/rpc-twoparty.h>
#include <gtest/gtest.h>
#include "laminar.capnp.h"
#include "eventsource.h"
#include "tempdir.h"
#include "laminar.h"
#include "server.h"
#include "conf.h"

class LaminarFixture : public ::testing::Test {
public:
    LaminarFixture() {
        bind_rpc = std::string("unix:/") + tmp.path.toString(true).cStr() + "/rpc.sock";
        bind_http = std::string("unix:/") + tmp.path.toString(true).cStr() + "/http.sock";
        home = tmp.path.toString(true).cStr();
        tmp.fs->openSubdir(kj::Path{"cfg", "jobs"}, kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT);
        settings.home = home.c_str();
        settings.bind_rpc = bind_rpc.c_str();
        settings.bind_http = bind_http.c_str();
        settings.archive_url = "/test-archive/";
        server = new Server(*ioContext);
        laminar = new Laminar(*server, settings);
    }
    ~LaminarFixture() noexcept(true) {
        delete server;
        delete laminar;
    }

    kj::Own<EventSource> eventSource(const char* path) {
        return kj::heap<EventSource>(*ioContext, bind_http.c_str(), path);
    }

    void defineJob(const char* name, const char* scriptContent, const char* configContent = nullptr) {
        KJ_IF_MAYBE(f, tmp.fs->tryOpenFile(kj::Path{"cfg", "jobs", std::string(name) + ".run"},
                kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT | kj::WriteMode::EXECUTABLE)) {
            (*f)->writeAll(std::string("#!/bin/sh\n") + scriptContent + "\n");
        }
        if(configContent) {
            KJ_IF_MAYBE(f, tmp.fs->tryOpenFile(kj::Path{"cfg", "jobs", std::string(name) + ".conf"}, kj::WriteMode::CREATE)) {
                (*f)->writeAll(configContent);
            }
        }
    }

    struct RunExec {
        LaminarCi::JobResult result;
        kj::String log;
    };

    RunExec runJob(const char* name, kj::Maybe<StringMap> params = nullptr) {
        auto req = client().runRequest();
        req.setJobName(name);
        KJ_IF_MAYBE(p, params) {
            auto params = req.initParams(p->size());
            int i = 0;
            for(auto kv : *p) {
                params[i].setName(kv.first);
                params[i].setValue(kv.second);
                i++;
            }
        }
        auto res = req.send().wait(ioContext->waitScope);
        std::string path = std::string{"/log/"} + name + "/" + std::to_string(res.getBuildNum());
        kj::HttpHeaderTable headerTable;
        kj::String log = kj::newHttpClient(ioContext->lowLevelProvider->getTimer(), headerTable,
                                           *ioContext->provider->getNetwork().parseAddress(bind_http.c_str()).wait(ioContext->waitScope))
                ->request(kj::HttpMethod::GET, path, kj::HttpHeaders(headerTable)).response.wait(ioContext->waitScope).body
                ->readAllText().wait(ioContext->waitScope);
        return { res.getResult(), kj::mv(log) };
    }

    kj::String stripLaminarLogLines(const kj::String& str) {
        auto out = kj::heapString(str.size());
        char *o = out.begin();
        for(const char *p = str.cStr(), *e = p + str.size(); p < e;) {
            const char *nl = strchrnul(p, '\n');
            if(!kj::StringPtr{p}.startsWith("[laminar]")) {
                memcpy(o, p, nl - p + 1);
                o += nl - p + 1;
            }
            p = nl + 1;
        }
        *o = '\0';
        return out;
    }

    StringMap parseFromString(kj::StringPtr content) {
        char tmp[16] = "/tmp/lt.XXXXXX";
        int fd = mkstemp(tmp);
        write(fd, content.begin(), content.size());
        close(fd);
        StringMap map = parseConfFile(tmp);
        unlink(tmp);
        return map;
    }

    LaminarCi::Client client() {
        if(!rpc) {
            auto stream = ioContext->provider->getNetwork().parseAddress(bind_rpc).wait(ioContext->waitScope)->connect().wait(ioContext->waitScope);
            auto net = kj::heap<capnp::TwoPartyVatNetwork>(*stream, capnp::rpc::twoparty::Side::CLIENT);
            rpc = kj::heap<capnp::RpcSystem<capnp::rpc::twoparty::VatId>>(*net, nullptr).attach(kj::mv(net), kj::mv(stream));
        }
        static capnp::word scratch[4];
        memset(scratch, 0, sizeof(scratch));
        auto hostId = capnp::MallocMessageBuilder(scratch).getRoot<capnp::rpc::twoparty::VatId>();
        hostId.setSide(capnp::rpc::twoparty::Side::SERVER);
        return rpc->bootstrap(hostId).castAs<LaminarCi>();
    }

    kj::Own<capnp::RpcSystem<capnp::rpc::twoparty::VatId>> rpc;
    TempDir tmp;
    std::string home, bind_rpc, bind_http;
    Settings settings;
    Server* server;
    Laminar* laminar;
    static kj::AsyncIoContext* ioContext;
};

#endif // LAMINAR_FIXTURE_H_
