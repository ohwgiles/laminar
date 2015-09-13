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
/// Laminaris distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with Laminar.  If not, see <http://www.gnu.org/licenses/>
///
#ifndef _LAMINAR_RESOURCES_H_
#define _LAMINAR_RESOURCES_H_

#include <unordered_map>
#include <utility>
#include <string>

// Simple class to abstract away the mapping of HTTP requests for
// resources to their data.
class Resources {
public:
    Resources();

    // If a resource is known for the given path, set start and end to the
    // binary data to send to the client. Function returns false if no resource
    // for the given path is known (404)
    bool handleRequest(std::string path, const char** start, const char** end);

private:
    std::unordered_map<std::string, std::pair<const char*, const char*>> resources;
};

#endif // _LAMINAR_RESOURCES_H_
