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
#include "conf.h"

#include <fstream>

template <>
int StringMap::convert(std::string e) { return atoi(e.c_str()); }

StringMap parseConfFile(const char* path) {
    StringMap result;
    std::ifstream f(path);
    std::string line;
    while(std::getline(f, line)) {
        if(line[0] == '#')
            continue;
        size_t p = line.find('=');
        if(p != std::string::npos) {
            result.emplace(line.substr(0, p), line.substr(p+1));
        }
    }
    return result;
}
