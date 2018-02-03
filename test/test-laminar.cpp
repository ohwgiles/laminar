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
#include <rapidjson/stringbuffer.h>
#include <rapidjson/document.h>
#include "laminar.h"

class TestLaminarClient : public LaminarClient {
public:
    virtual void sendMessage(std::string p) { payload = p; }
    std::string payload;
};

class LaminarTest : public ::testing::Test {
protected:
    Laminar laminar;
};

