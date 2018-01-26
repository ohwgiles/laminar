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
#include "conf.h"

class ConfTest : public ::testing::Test {
protected:
    void SetUp() override {
        fd = mkstemp(tmpFile);
    }
    void TearDown() override {
        close(fd);
        unlink(tmpFile);
    }
    void parseConf(std::string conf) {
        lseek(fd, SEEK_SET, 0);
        write(fd, conf.data(), conf.size());
        cfg = parseConfFile(tmpFile);
    }
    StringMap cfg;
    int fd;
    char tmpFile[32] = "/tmp/lt.XXXXXX";
};

TEST_F(ConfTest, Empty) {
    EXPECT_TRUE(cfg.empty());
    parseConf("");
    EXPECT_TRUE(cfg.empty());
}

TEST_F(ConfTest, Comments) {
    parseConf("#");
    EXPECT_TRUE(cfg.empty());
    parseConf("#foo=bar");
    EXPECT_TRUE(cfg.empty());
}

TEST_F(ConfTest, Parse) {
    parseConf("foo=bar\nbar=3");
    ASSERT_EQ(2, cfg.size());
    EXPECT_EQ("bar", cfg.get("foo", std::string("fallback")));
    EXPECT_EQ(3, cfg.get("bar", 0));
}

TEST_F(ConfTest, Fallback) {
    EXPECT_EQ("foo", cfg.get("test", std::string("foo")));
}
