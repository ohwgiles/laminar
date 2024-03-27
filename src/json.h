///
/// Copyright 2015-2022 Oliver Giles
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
#ifndef LAMINAR_JSON_H_
#define LAMINAR_JSON_H_

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <string>

// rapidjson::Writer with a StringBuffer is used a lot in Laminar for
// preparing JSON messages to send to HTTP clients. A small wrapper
// class here reduces verbosity later for this common use case.
class Json : public rapidjson::Writer<rapidjson::StringBuffer> {
public:
    Json() : rapidjson::Writer<rapidjson::StringBuffer>(buf) { StartObject(); }
    template<typename T>
    Json& set(const char* key, T value) { String(key); Int64(value); return *this; }
    Json& startObject(const char* key) { String(key); StartObject(); return *this; }
    Json& startArray(const char* key) { String(key); StartArray(); return *this; }
    Json& setJsonObject(const char* key, const std::string& object);
    const char* str() { EndObject(); return buf.GetString(); }
private:
    rapidjson::StringBuffer buf;
};

template<> Json& Json::set(const char* key, double value);
template<> Json& Json::set(const char* key, const char* value);
template<> Json& Json::set(const char* key, std::string value);


#endif // ? ! LAMINAR_LAMINAR_H_
