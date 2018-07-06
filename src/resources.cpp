///
/// Copyright 2015-2017 Oliver Giles
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
#include "resources.h"
#include <string.h>

#define INIT_RESOURCE(route, name, content_type) \
    extern const char _binary_##name##_z_start[];\
    extern const char _binary_##name##_z_end[]; \
    resources.emplace(route, Resource{_binary_ ## name ## _z_start, _binary_ ## name ## _z_end, content_type})

#define CONTENT_TYPE_HTML "text/html; charset=utf-8"
#define CONTENT_TYPE_ICO  "image/x-icon"
#define CONTENT_TYPE_PNG  "image/png"
#define CONTENT_TYPE_JS   "application/javascript; charset=utf-8"
#define CONTENT_TYPE_CSS  "text/css; charset=utf-8"

Resources::Resources()
{
    INIT_RESOURCE("/", index_html, CONTENT_TYPE_HTML);
    INIT_RESOURCE("/favicon.ico", favicon_ico, CONTENT_TYPE_ICO);
    INIT_RESOURCE("/favicon-152.png", favicon_152_png, CONTENT_TYPE_PNG);
    INIT_RESOURCE("/icon.png", icon_png, CONTENT_TYPE_PNG);
    INIT_RESOURCE("/js/app.js", js_app_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/js/ansi_up.js", js_ansi_up_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/js/vue.min.js", js_vue_min_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/js/vue-router.min.js", js_vue_router_min_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/js/ansi_up.js", js_ansi_up_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/js/Chart.min.js", js_Chart_min_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/css/bootstrap.min.css", css_bootstrap_min_css, CONTENT_TYPE_CSS);
}

inline bool beginsWith(std::string haystack, const char* needle) {
    return strncmp(haystack.c_str(), needle, strlen(needle)) == 0;
}

bool Resources::handleRequest(std::string path, const char** start, const char** end, const char** content_type) {
    // need to keep the list of "application links" synchronised with the angular
    // application. We cannot return a 404 for any of these
    auto it = beginsWith(path,"/jobs")
            ? resources.find("/")
            : resources.find(path);

    if(it != resources.end()) {
        *start = it->second.start;
        *end = it->second.end;
        *content_type = it->second.content_type;
        return true;
    }

    return false;
}

