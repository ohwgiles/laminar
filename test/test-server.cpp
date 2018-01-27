///
/// Copyright 2018 Oliver Giles
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
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/filesystem.hpp>
#include <thread>
#include <sys/socket.h>
#include "server.h"
#include "log.h"
#include "interface.h"
#include "laminar.capnp.h"

namespace fs = boost::filesystem;

class TempDir : public fs::path {
public:
    TempDir(const char* tmpl) {
        char* t = strdup(tmpl);
        mkdtemp(t);
        *static_cast<fs::path*>(this) = t;
        free(t);
    }
    ~TempDir() {
        fs::remove_all(*this);
    }
};

class MockLaminar : public LaminarInterface {
public:
    MOCK_METHOD1(registerClient, void(LaminarClient*));
    MOCK_METHOD1(deregisterClient, void(LaminarClient*));
    MOCK_METHOD2(queueJob, std::shared_ptr<Run>(std::string name, ParamMap params));
    MOCK_METHOD1(registerWaiter, void(LaminarWaiter* waiter));
    MOCK_METHOD1(deregisterWaiter, void(LaminarWaiter* waiter));
    MOCK_METHOD1(sendStatus, void(LaminarClient* client));
    MOCK_METHOD4(setParam, bool(std::string job, uint buildNum, std::string param, std::string value));
    MOCK_METHOD2(getArtefact, bool(std::string path, std::string& result));
    MOCK_METHOD0(getCustomCss, std::string());
};

class ServerTest : public ::testing::Test {
protected:
    ServerTest() :
        tempDir("/tmp/laminar-test-XXXXXX")
    {
    }
    void SetUp() override {
        EXPECT_CALL(mockLaminar, registerWaiter(testing::_));
        EXPECT_CALL(mockLaminar, deregisterWaiter(testing::_));
        server = new Server(mockLaminar, "unix:"+fs::path(tempDir/"rpc.sock").string(), "127.0.0.1:8080");
    }
    void TearDown() override {
        delete server;
    }

    LaminarCi::Client client() const {
        return server->rpcInterface.castAs<LaminarCi>();
    }
    kj::WaitScope& ws() const {
        return server->ioContext.waitScope;
    }
    TempDir tempDir;
    MockLaminar mockLaminar;
    Server* server;
};

TEST_F(ServerTest, RpcTrigger) {
    auto req = client().triggerRequest();
    req.setJobName("foo");
    EXPECT_CALL(mockLaminar, queueJob("foo", ParamMap())).Times(testing::Exactly(1));
    req.send().wait(ws());
}
