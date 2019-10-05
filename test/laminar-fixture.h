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
        server = new Server(ioContext);
        laminar = new Laminar(*server, settings);
    }
    ~LaminarFixture() noexcept(true) {
        delete server;
        delete laminar;
    }

    kj::Own<EventSource> eventSource(const char* path) {
        return kj::heap<EventSource>(ioContext, bind_http.c_str(), path);
    }

    void defineJob(const char* name, const char* scriptContent) {
        KJ_IF_MAYBE(f, tmp.fs->tryOpenFile(kj::Path{"cfg", "jobs", std::string(name) + ".run"},
                kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT | kj::WriteMode::EXECUTABLE)) {
            (*f)->writeAll(std::string("#!/bin/sh\n") + scriptContent + "\n");
        }
    }

    LaminarCi::Client client() {
        if(!rpc) {
            auto stream = ioContext.provider->getNetwork().parseAddress(bind_rpc).wait(ioContext.waitScope)->connect().wait(ioContext.waitScope);
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
    static kj::AsyncIoContext ioContext;
};

#endif // LAMINAR_FIXTURE_H_
