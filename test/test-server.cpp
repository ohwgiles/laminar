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
#include <thread>
#include <sys/socket.h>
#include "server.h"
#include "log.h"
#include "interface.h"
#include "laminar.capnp.h"
#include "tempdir.h"

class MockLaminar : public LaminarInterface {
public:
    LaminarClient* client = nullptr;
    ~MockLaminar() {}
    virtual void registerClient(LaminarClient* c) override {
        ASSERT_EQ(nullptr, client);
        client = c;
        EXPECT_CALL(*this, sendStatus(client)).Times(testing::Exactly(1));
    }

    virtual void deregisterClient(LaminarClient* c) override {
        ASSERT_EQ(client, c);
        client = nullptr;
    }

    // MOCK_METHOD does not seem to work with return values whose destructors have noexcept(false)
    kj::Maybe<kj::Own<const kj::ReadableFile>> getArtefact(std::string path) override { return nullptr; }

    MOCK_METHOD2(queueJob, std::shared_ptr<Run>(std::string name, ParamMap params));
    MOCK_METHOD1(registerWaiter, void(LaminarWaiter* waiter));
    MOCK_METHOD1(deregisterWaiter, void(LaminarWaiter* waiter));
    MOCK_METHOD1(sendStatus, void(LaminarClient* client));
    MOCK_METHOD4(setParam, bool(std::string job, uint buildNum, std::string param, std::string value));
    MOCK_METHOD0(listQueuedJobs, const std::list<std::shared_ptr<Run>>&());
    MOCK_METHOD0(listRunningJobs, const RunSet&());
    MOCK_METHOD0(listKnownJobs, std::list<std::string>());

    MOCK_METHOD0(getCustomCss, std::string());
    MOCK_METHOD2(handleBadgeRequest, bool(std::string, std::string&));
    MOCK_METHOD2(abort, bool(std::string, uint));
    MOCK_METHOD0(abortAll, void());
    MOCK_METHOD0(notifyConfigChanged, void());
};

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        EXPECT_CALL(mockLaminar, registerWaiter(testing::_));
        EXPECT_CALL(mockLaminar, deregisterWaiter(testing::_));
        server = new Server(mockLaminar, "unix:"+std::string(tempDir.path.append("rpc.sock").toString(true).cStr()), "127.0.0.1:8080");
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
    void waitForHttpReady() {
        server->httpReady.promise.wait(server->ioContext.waitScope);
    }

    kj::Network& network() { return server->ioContext.provider->getNetwork(); }
    TempDir tempDir;
    MockLaminar mockLaminar;
    Server* server;
};

TEST_F(ServerTest, RpcQueue) {
    auto req = client().queueRequest();
    req.setJobName("foo");
    EXPECT_CALL(mockLaminar, queueJob("foo", ParamMap())).Times(testing::Exactly(1));
    req.send().wait(ws());
}

// Tests that agressively closed websockets are properly removed
// and will not be attempted to be contacted again
TEST_F(ServerTest, HttpWebsocketRST) {
    waitForHttpReady();

    // TODO: generalize
    constexpr const char* WS =
        "GET / HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Connection: Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Key: GTFmrUCM9N6B32LdDE3Rzw==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    static char buffer[256];
    network().parseAddress("localhost:8080").then([this](kj::Own<kj::NetworkAddress>&& addr){
        return addr->connect().attach(kj::mv(addr)).then([this](kj::Own<kj::AsyncIoStream>&& stream){
            kj::AsyncIoStream* s = stream.get();
            return s->write(WS, strlen(WS)).then([this,s](){
                // Read the websocket header response, ensure the client has been registered
                return s->tryRead(buffer, 64, 256).then([this,s](size_t sz){
                    EXPECT_LE(64, sz);
                    EXPECT_NE(nullptr, mockLaminar.client);
                    // agressively abort the connection
                    struct linger so_linger;
                    so_linger.l_onoff = 1;
                    so_linger.l_linger = 0;
                    s->setsockopt(SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
                    return kj::Promise<void>(kj::READY_NOW);
                });
            }).attach(kj::mv(stream));
        });
    }).wait(ws());
    ws().poll();
    // Expect that the client has been cleared. If it has not, Laminar could
    // try to write to the closed file descriptor, causing an exception
    EXPECT_EQ(nullptr, mockLaminar.client);
}
