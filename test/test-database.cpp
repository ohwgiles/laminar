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
#include "database.h"

class DatabaseTest : public ::testing::Test {
protected:
    DatabaseTest() :
        ::testing::Test(),
        db(":memory:")
    {}
    Database db;
};

TEST_F(DatabaseTest, Exec) {
    EXPECT_FALSE(db.exec("garbage non-sql"));
    EXPECT_TRUE(db.exec("create temporary table test(id int)"));
}

TEST_F(DatabaseTest, Fetch) {
    int n = 0;
    db.stmt("select 2, 'cat', 4294967299").fetch<int, std::string, uint64_t>([&](int i, std::string s, uint64_t ui){
        n++;
        EXPECT_EQ(2, i);
        EXPECT_EQ("cat", s);
        EXPECT_EQ(4294967299, ui);
    });
    EXPECT_EQ(1, n);
}

TEST_F(DatabaseTest, Bind) {
    int n = 0;
    db.stmt("select ? * 2").bind(2).fetch<int>([&](int i){
        n++;
        EXPECT_EQ(4, i);
    });
    EXPECT_EQ(1, n);
}

TEST_F(DatabaseTest, Strings) {
    std::string res;
    db.stmt("select ? || ?").bind("a", "b").fetch<std::string>([&res](std::string s){
        EXPECT_TRUE(res.empty());
        res = s;
    });
    EXPECT_EQ("ab", res);
}

TEST_F(DatabaseTest, MultiRow) {
    ASSERT_TRUE(db.exec("create table test(id int)"));
    int i = 0;
    while(i < 10)
        EXPECT_TRUE(db.stmt("insert into test values(?)").bind(i++).exec());
    i = 0;
    db.stmt("select * from test").fetch<int>([&](int r){
        EXPECT_EQ(i++, r);
    });
    EXPECT_EQ(10, i);
}

TEST_F(DatabaseTest, StdevFunc) {
    double res = 0;
    db.stmt("with a (x) as (values (7),(3),(45),(23)) select stdev(x) from a").fetch<double>([&](double r){
        res = r;
    });
    EXPECT_FLOAT_EQ(19.0700463205171, res);
}
