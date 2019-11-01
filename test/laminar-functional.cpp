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

// TODO: consider handling this differently
kj::AsyncIoContext LaminarFixture::ioContext = kj::setupAsyncIo();

TEST_F(LaminarFixture, EmptyStatusMessageStructure) {
    auto es = eventSource("/");
    ioContext.waitScope.poll();
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
    EXPECT_TRUE(data.HasMember("buildTimeDist"));
}

TEST_F(LaminarFixture, JobNotifyHomePage) {
    defineJob("foo", "true");
    auto es = eventSource("/");

    auto req = client().runRequest();
    req.setJobName("foo");
    ASSERT_EQ(LaminarCi::JobResult::SUCCESS, req.send().wait(ioContext.waitScope).getResult());

    // wait for job completed
    ioContext.waitScope.poll();

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

    auto req1 = client().runRequest();
    req1.setJobName("job1");
    ASSERT_EQ(LaminarCi::JobResult::SUCCESS, req1.send().wait(ioContext.waitScope).getResult());
    auto req2 = client().runRequest();
    req2.setJobName("job2");
    ASSERT_EQ(LaminarCi::JobResult::SUCCESS, req2.send().wait(ioContext.waitScope).getResult());
    ioContext.waitScope.poll();

    EXPECT_EQ(7, esHome->messages().size());
    EXPECT_EQ(7, esJobs->messages().size());

    EXPECT_EQ(4, es1Job->messages().size());
    EXPECT_EQ(4, es2Job->messages().size());

    EXPECT_EQ(4, es1Run->messages().size());
    EXPECT_EQ(4, es2Run->messages().size());
}
