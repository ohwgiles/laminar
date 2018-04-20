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
#include "run.h"
#include "log.h"
#include "node.h"
#include "conf.h"

class RunTest : public ::testing::Test {
protected:
    void SetUp() override {
        run.node = node;
    }
    void wait() {
        int state = -1;
        waitpid(run.pid, &state, 0);
        run.reaped(state);
    }
    void runAll() {
        while(!run.step())
            wait();
    }
    std::string readAllOutput() {
        std::string res;
        char tmp[64];
        for(ssize_t n = read(run.fd, tmp, 64); n > 0; n = read(run.fd, tmp, 64))
            res += std::string(tmp, n);
        // strip the first "[laminar] executing.. line
        return strchr(res.c_str(), '\n') + 1;
    }
    StringMap parseFromString(std::string content) {
        char tmp[16] = "/tmp/lt.XXXXXX";
        int fd = mkstemp(tmp);
        write(fd, content.data(), content.size());
        close(fd);
        StringMap map = parseConfFile(tmp);
        unlink(tmp);
        return map;
    }

    class Run run;
    std::shared_ptr<Node> node = std::shared_ptr<Node>(new Node);
};

TEST_F(RunTest, WorkingDirectory) {
    run.addScript("/bin/pwd", "/home");
    runAll();
    EXPECT_EQ("/home\n", readAllOutput());
}

TEST_F(RunTest, SuccessStatus) {
    run.addScript("/bin/true");
    runAll();
    EXPECT_EQ(RunState::SUCCESS, run.result);
}

TEST_F(RunTest, FailedStatus) {
    run.addScript("/bin/false");
    runAll();
    EXPECT_EQ(RunState::FAILED, run.result);
}

TEST_F(RunTest, Environment) {
    run.name = "foo";
    run.build = 1234;
    run.laminarHome = "/tmp";
    run.addScript("/usr/bin/env");
    runAll();
    StringMap map = parseFromString(readAllOutput());
    EXPECT_EQ("1234", map["RUN"]);
    EXPECT_EQ("foo", map["JOB"]);
    EXPECT_EQ("success", map["RESULT"]);
    EXPECT_EQ("unknown", map["LAST_RESULT"]);
    EXPECT_EQ("/tmp/run/foo/workspace", map["WORKSPACE"]);
    EXPECT_EQ("/tmp/archive/foo/1234", map["ARCHIVE"]);
}

TEST_F(RunTest, ParamsToEnv) {
    run.params["foo"] = "bar";
    run.addScript("/usr/bin/env");
    runAll();
    StringMap map = parseFromString(readAllOutput());
    EXPECT_EQ("bar", map["foo"]);
}

TEST_F(RunTest, Abort) {
    run.addScript("/usr/bin/yes");
    run.step();
    usleep(200); // TODO fix
    run.abort();
    wait();
    EXPECT_EQ(RunState::ABORTED, run.result);
}

TEST_F(RunTest, AbortAfterFailed) {
    run.addScript("/bin/false");
    runAll();
    run.addScript("/usr/bin/yes");
    run.step();
    usleep(200); // TODO fix
    run.abort();
    wait();
    EXPECT_EQ(RunState::FAILED, run.result);
}
