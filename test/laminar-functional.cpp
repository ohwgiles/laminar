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
#include <kj/async-unix.h>
#include "laminar-fixture.h"
#include "conf.h"

// TODO: consider handling this differently
kj::AsyncIoContext* LaminarFixture::ioContext;

TEST_F(LaminarFixture, EmptyStatusMessageStructure) {
    auto es = eventSource("/");
    ioContext->waitScope.poll();
    ASSERT_EQ(1, es->messages().size());

    auto json = es->messages().front().GetObject();
    EXPECT_STREQ("status", json["type"].GetString());
    EXPECT_STREQ("Laminar", json["title"].GetString());
    EXPECT_LT(time(nullptr) - json["time"].GetInt64(), 1);

    auto data = json["data"].GetObject();
    EXPECT_TRUE(data.HasMember("recent"));
    EXPECT_TRUE(data.HasMember("running"));
    EXPECT_TRUE(data.HasMember("queued"));
    EXPECT_TRUE(data.HasMember("executorsTotal"));
    EXPECT_TRUE(data.HasMember("executorsBusy"));
    EXPECT_TRUE(data.HasMember("buildsPerDay"));
    EXPECT_TRUE(data.HasMember("buildsPerJob"));
    EXPECT_TRUE(data.HasMember("timePerJob"));
    EXPECT_TRUE(data.HasMember("resultChanged"));
    EXPECT_TRUE(data.HasMember("lowPassRates"));
    EXPECT_TRUE(data.HasMember("buildTimeChanges"));
}

TEST_F(LaminarFixture, JobNotifyHomePage) {
    defineJob("foo", "true");
    auto es = eventSource("/");

    runJob("foo");

    ASSERT_EQ(4, es->messages().size());

    auto job_queued = es->messages().at(1).GetObject();
    EXPECT_STREQ("job_queued", job_queued["type"].GetString());
    EXPECT_STREQ("foo", job_queued["data"]["name"].GetString());

    auto job_started = es->messages().at(2).GetObject();
    EXPECT_STREQ("job_started", job_started["type"].GetString());
    EXPECT_STREQ("foo", job_started["data"]["name"].GetString());

    auto job_completed = es->messages().at(3).GetObject();
    EXPECT_STREQ("job_completed", job_completed["type"].GetString());
    EXPECT_STREQ("foo", job_completed["data"]["name"].GetString());
}

TEST_F(LaminarFixture, OnlyRelevantNotifications) {
    defineJob("job1", "true");
    defineJob("job2", "true");

    auto esHome = eventSource("/");
    auto esJobs = eventSource("/jobs");
    auto es1Job = eventSource("/jobs/job1");
    auto es2Job = eventSource("/jobs/job2");
    auto es1Run = eventSource("/jobs/job1/1");
    auto es2Run = eventSource("/jobs/job2/1");

    runJob("job1");
    runJob("job2");

    EXPECT_EQ(7, esHome->messages().size());
    EXPECT_EQ(7, esJobs->messages().size());

    EXPECT_EQ(4, es1Job->messages().size());
    EXPECT_EQ(4, es2Job->messages().size());

    EXPECT_EQ(4, es1Run->messages().size());
    EXPECT_EQ(4, es2Run->messages().size());
}

TEST_F(LaminarFixture, FailedStatus) {
    defineJob("job1", "false");
    auto run = runJob("job1");
    ASSERT_EQ(LaminarCi::JobResult::FAILED, run.result);
}

TEST_F(LaminarFixture, WorkingDirectory) {
    defineJob("job1", "pwd");
    auto run = runJob("job1");
    ASSERT_EQ(LaminarCi::JobResult::SUCCESS, run.result);
    std::string cwd{tmp.path.append(kj::Path{"run","job1","1"}).toString(true).cStr()};
    EXPECT_EQ(cwd + "\n", stripLaminarLogLines(run.log).cStr());
}


TEST_F(LaminarFixture, Environment) {
    defineJob("foo", "env");
    auto run = runJob("foo");

    std::string ws{tmp.path.append(kj::Path{"run","foo","workspace"}).toString(true).cStr()};
    std::string archive{tmp.path.append(kj::Path{"archive","foo","1"}).toString(true).cStr()};

    StringMap map = parseFromString(run.log);
    EXPECT_EQ("1", map["RUN"]);
    EXPECT_EQ("foo", map["JOB"]);
    EXPECT_EQ("success", map["RESULT"]);
    EXPECT_EQ("unknown", map["LAST_RESULT"]);
    EXPECT_EQ(ws, map["WORKSPACE"]);
    EXPECT_EQ(archive, map["ARCHIVE"]);
}

TEST_F(LaminarFixture, ParamsToEnv) {
    defineJob("foo", "env");
    StringMap params;
    params["foo"] = "bar";
    auto run = runJob("foo", params);
    StringMap map = parseFromString(run.log);
    EXPECT_EQ("bar", map["foo"]);
}

TEST_F(LaminarFixture, Abort) {
    defineJob("job1", "sleep inf");
    auto req = client().runRequest();
    req.setJobName("job1");
    auto res = req.send();
    // There isn't a nice way of knowing when the leader process is ready to
    // handle SIGTERM. Just wait until it prints something to the log
    ioContext->waitScope.poll();
    kj::HttpHeaderTable headerTable;
    char _;
    kj::newHttpClient(ioContext->lowLevelProvider->getTimer(), headerTable,
                      *ioContext->provider->getNetwork().parseAddress(bind_http.c_str()).wait(ioContext->waitScope))
            ->request(kj::HttpMethod::GET, "/log/job1/1", kj::HttpHeaders(headerTable)).response.wait(ioContext->waitScope).body
            ->tryRead(&_, 1, 1).wait(ioContext->waitScope);
    // now it should be ready to abort
    ASSERT_TRUE(laminar->abort("job1", 1));
    EXPECT_EQ(LaminarCi::JobResult::ABORTED, res.wait(ioContext->waitScope).getResult());
}

TEST_F(LaminarFixture, JobDescription) {
    defineJob("foo", "true", "DESCRIPTION=bar");
    auto es = eventSource("/jobs/foo");
    ioContext->waitScope.poll();
    ASSERT_EQ(1, es->messages().size());
    auto json = es->messages().front().GetObject();
    ASSERT_TRUE(json.HasMember("data"));
    auto data = json["data"].GetObject();
    ASSERT_TRUE(data.HasMember("description"));
    EXPECT_STREQ("bar", data["description"].GetString());
}
