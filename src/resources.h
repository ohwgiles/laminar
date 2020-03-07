///
/// Copyright 2015-2019 Oliver Giles
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
#ifndef LAMINAR_RESOURCES_H_
#define LAMINAR_RESOURCES_H_

#include <unordered_map>
#include <utility>
#include <string>

// Simple class to abstract away the mapping of HTTP requests for
// resources to their data.
class Resources {
public:
    Resources();

    // If a resource is known for the given path, set start and end to the
    // binary data to send to the client, and content_type to its MIME
    // type. Function returns false if no resource for the given path exists
    bool handleRequest(std::string path, const char** start, const char** end, const char** content_type);

    // Allows providing a custom HTML template. Pass an empty string to use the default.
    void setHtmlTemplate(std::string templ = std::string());

private:
    struct Resource {
        const char* start;
        const char* end;
        const char* content_type;
    };
    std::unordered_map<std::string, Resource> resources;
    std::string index_html;
};

#endif // LAMINAR_RESOURCES_H_
