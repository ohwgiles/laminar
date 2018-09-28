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
#include "tempdir.h"

class RunTest : public testing::Test {
protected:
    RunTest() :
        testing::Test(),
        node(std::make_shared<Node>()),
        tmp(),
        run("foo", ParamMap{}, tmp.path.clone())
    {
    }

    ~RunTest() noexcept {}

    void wait() {
        int state = -1;
        waitpid(run.current_pid.orDefault(0), &state, 0);
        run.reaped(state);
    }

    void runAll() {
        while(!run.step())
            wait();
    }

    std::string readAllOutput() {
        std::string res;
        char tmp[64];
        for(ssize_t n = read(run.output_fd, tmp, 64); n > 0; n = read(run.output_fd, tmp, 64))
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

    std::shared_ptr<Node> node;
    TempDir tmp;
    class Run run;

    void setRunLink(const char* path) {
        tmp.fs->symlink(kj::Path{"cfg","jobs",run.name+".run"}, path, kj::WriteMode::CREATE|kj::WriteMode::CREATE_PARENT|kj::WriteMode::EXECUTABLE);
    }
};

TEST_F(RunTest, WorkingDirectory) {
    setRunLink("/bin/pwd");
    run.configure(1, node, *tmp.fs);
    runAll();
    std::string cwd{tmp.path.append(kj::Path{"run","foo","1"}).toString(true).cStr()};
    EXPECT_EQ(cwd + "\n", readAllOutput());
}

TEST_F(RunTest, SuccessStatus) {
    setRunLink("/bin/true");
    run.configure(1, node, *tmp.fs);
    runAll();
    EXPECT_EQ(RunState::SUCCESS, run.result);
}

TEST_F(RunTest, FailedStatus) {
    setRunLink("/bin/false");
    run.configure(1, node, *tmp.fs);
    runAll();
    EXPECT_EQ(RunState::FAILED, run.result);
}

TEST_F(RunTest, Environment) {
    setRunLink("/usr/bin/env");
    run.configure(1234, node, *tmp.fs);
    runAll();

    std::string ws{tmp.path.append(kj::Path{"run","foo","workspace"}).toString(true).cStr()};
    std::string archive{tmp.path.append(kj::Path{"archive","foo","1234"}).toString(true).cStr()};

    StringMap map = parseFromString(readAllOutput());
    EXPECT_EQ("1234", map["RUN"]);
    EXPECT_EQ("foo", map["JOB"]);
    EXPECT_EQ("success", map["RESULT"]);
    EXPECT_EQ("unknown", map["LAST_RESULT"]);
    EXPECT_EQ(ws, map["WORKSPACE"]);
    EXPECT_EQ(archive, map["ARCHIVE"]);
}

TEST_F(RunTest, ParamsToEnv) {
    setRunLink("/usr/bin/env");
    run.params["foo"] = "bar";
    run.configure(1, node, *tmp.fs);
    runAll();
    StringMap map = parseFromString(readAllOutput());
    EXPECT_EQ("bar", map["foo"]);
}

TEST_F(RunTest, Abort) {
    setRunLink("/usr/bin/yes");
    run.configure(1, node, *tmp.fs);
    run.step();
    usleep(200); // TODO fix
    run.abort(false);
    wait();
    EXPECT_EQ(RunState::ABORTED, run.result);
}

