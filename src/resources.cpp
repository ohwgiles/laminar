///
/// Copyright 2015-2020 Oliver Giles
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
#include "log.h"
#include "index_html_size.h"
#include <string.h>
#include <zlib.h>

#define INIT_RESOURCE(route, name, content_type) \
    extern const char _binary_##name##_z_start[];\
    extern const char _binary_##name##_z_end[]; \
    resources.emplace(route, Resource{_binary_ ## name ## _z_start, _binary_ ## name ## _z_end, content_type})

#define CONTENT_TYPE_HTML     "text/html; charset=utf-8"
#define CONTENT_TYPE_ICO      "image/x-icon"
#define CONTENT_TYPE_PNG      "image/png"
#define CONTENT_TYPE_JS       "application/javascript; charset=utf-8"
#define CONTENT_TYPE_CSS      "text/css; charset=utf-8"
#define CONTENT_TYPE_MANIFEST "application/manifest+json; charset=utf-8"

#define GZIP_FORMAT 16

Resources::Resources()
{
    INIT_RESOURCE("/favicon.ico", favicon_ico, CONTENT_TYPE_ICO);
    INIT_RESOURCE("/favicon-152.png", favicon_152_png, CONTENT_TYPE_PNG);
    INIT_RESOURCE("/icon.png", icon_png, CONTENT_TYPE_PNG);
    INIT_RESOURCE("/js/app.js", js_app_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/js/ansi_up.js", js_ansi_up_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/js/vue.min.js", js_vue_min_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/js/ansi_up.js", js_ansi_up_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/js/Chart.min.js", js_Chart_min_js, CONTENT_TYPE_JS);
    INIT_RESOURCE("/style.css", style_css, CONTENT_TYPE_CSS);
    INIT_RESOURCE("/manifest.webmanifest", manifest_webmanifest, CONTENT_TYPE_MANIFEST);
    // Configure the default template
    setHtmlTemplate(std::string());
}

void Resources::setHtmlTemplate(std::string tmpl) {
    extern const char _binary_index_html_z_start[];
    extern const char _binary_index_html_z_end[];

    z_stream strm;
    memset(&strm, 0, sizeof(z_stream));

    if(!tmpl.empty()) {
        // deflate
        index_html.resize(tmpl.size());
        deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS|GZIP_FORMAT, 8, Z_DEFAULT_STRATEGY);
        strm.next_in = (unsigned char*) tmpl.data();
        strm.avail_in = tmpl.size();
        strm.next_out = (unsigned char*) index_html.data();
        strm.avail_out = tmpl.size();
        if(deflate(&strm, Z_FINISH) != Z_STREAM_END) {
            LLOG(FATAL, "Failed to compress index.html");
        }
        index_html.resize(strm.total_out);
    } else {
        // use the default template from compile-time asset
        if(const char* baseUrl = getenv("LAMINAR_BASE_URL")) {
            // The administrator needs to customize the <base href>. Unfortunately this seems
            // to be the only thing that needs to be customizable but cannot be done via dynamic
            // DOM manipulation without heavy compromises. So replace the static char array with
            // a modified buffer accordingly.
            std::string tmp;
            tmp.resize(INDEX_HTML_UNCOMPRESSED_SIZE);
            // inflate
            inflateInit2(&strm, MAX_WBITS|GZIP_FORMAT);
            strm.next_in = (unsigned char*) _binary_index_html_z_start;
            strm.avail_in = _binary_index_html_z_end - _binary_index_html_z_start;
            strm.next_out = (unsigned char*) tmp.data();
            strm.avail_out = INDEX_HTML_UNCOMPRESSED_SIZE;
            if(inflate(&strm, Z_FINISH) != Z_STREAM_END) {
                LLOG(FATAL, "Failed to uncompress index_html");
            }
            // replace
            // There's no validation on the replacement string, so you can completely mangle
            // the html if you like. This isn't really an issue because if you can modify laminar's
            // environment you already have elevated permissions
            if(auto it = tmp.find("base href=\"/"))
                tmp.replace(it+11, 1, baseUrl);
            // deflate
            index_html.resize(tmp.size());
            deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS|GZIP_FORMAT, 8, Z_DEFAULT_STRATEGY);
            strm.next_in = (unsigned char*) tmp.data();
            strm.avail_in = tmp.size();
            strm.next_out = (unsigned char*) index_html.data();
            strm.avail_out = tmp.size();
            if(deflate(&strm, Z_FINISH) != Z_STREAM_END) {
                LLOG(FATAL, "Failed to compress index.html");
            }
            index_html.resize(strm.total_out);
        } else {
            index_html = std::string(_binary_index_html_z_start, _binary_index_html_z_end);
        }
    }
    // update resource map
    resources["/"] = Resource{index_html.data(), index_html.data() + index_html.size(), CONTENT_TYPE_HTML};
}

inline bool beginsWith(std::string haystack, const char* needle) {
    return strncmp(haystack.c_str(), needle, strlen(needle)) == 0;
}

bool Resources::handleRequest(std::string path, const char** start, const char** end, const char** content_type) {
    // need to keep the list of "application links" synchronised with the angular
    // application. We cannot return a 404 for any of these
    auto it = beginsWith(path,"/jobs") || path == "/wallboard"
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

