///
/// Copyright 2015 Oliver Giles
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
#ifndef LAMINAR_CONF_H_
#define LAMINAR_CONF_H_

#include <string>
#include <unordered_map>

class StringMap : public std::unordered_map<std::string, std::string> {
public:
    template<typename T>
    T get(std::string key, T fallback = T()) {
        auto it = find(key);
        return it != end() ? convert<T>(it->second) : fallback;
    }
private:
    template<typename T>
    T convert(std::string e) { return e; }
};
template <>
int StringMap::convert(std::string e);

// Reads a file by line into a list of key/value pairs
// separated by the first '=' character. Discards lines
// beginning with '#'
StringMap parseConfFile(const char* path);


#endif // LAMINAR_CONF_H_
