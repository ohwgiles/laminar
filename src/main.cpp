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
#include "laminar.h"
#include "leader.h"
#include "server.h"
#include "log.h"

#include <fcntl.h>
#include <iostream>
#include <kj/async-unix.h>
#include <kj/filesystem.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

static Laminar* laminar;
static Server* server;

static void laminar_quit(int) {
    // Abort current jobs. Most of the time this isn't necessary since
    // systemd stop or other kill mechanism will send SIGTERM to the whole
    // process group.
    laminar->abortAll();
    server->stop();
}

namespace {
constexpr const char* INTADDR_RPC_DEFAULT = "unix-abstract:laminar";
constexpr const char* INTADDR_HTTP_DEFAULT = "*:8080";
constexpr const char* ARCHIVE_URL_DEFAULT = "/archive/";
}

static void usage(std::ostream& out) {
    out << "laminard version " << laminar_version() << "\n";
    out << "Usage:\n";
    out << "  -h|--help       show this help message\n";
    out << "  -v              enable verbose output\n";
}

int main(int argc, char** argv) {
    if(argv[0][0] == '{')
        return leader_main();

    for(int i = 1; i < argc; ++i) {
        if(strcmp(argv[i], "-v") == 0) {
            kj::_::Debug::setLogLevel(kj::_::Debug::Severity::INFO);
        } else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return usage(std::cout), EXIT_SUCCESS;
        } else {
            std::cerr << "Unknown argument " << argv[i] << "\n";
            return usage(std::cerr), EXIT_FAILURE;
        }
    }

    // The parent process hopefully connected stdin to /dev/null, but
    // do it again here just in case. This is important because stdin
    // is inherited to job runs via the leader process, and some
    // processes misbehave if they can successfully block on reading
    // from stdin.
    close(STDIN_FILENO);
    LASSERT(open("/dev/null", O_RDONLY) == STDIN_FILENO);

    auto ioContext = kj::setupAsyncIo();

    Settings settings;
    // Default values when none were supplied in $LAMINAR_CONF_FILE (/etc/laminar.conf)
    settings.home = getenv("LAMINAR_HOME") ?: "/var/lib/laminar";
    settings.bind_rpc = getenv("LAMINAR_BIND_RPC") ?: INTADDR_RPC_DEFAULT;
    settings.bind_http = getenv("LAMINAR_BIND_HTTP") ?: INTADDR_HTTP_DEFAULT;
    settings.archive_url = getenv("LAMINAR_ARCHIVE_URL") ?: ARCHIVE_URL_DEFAULT;

    server = new Server(ioContext);
    laminar = new Laminar(*server, settings);

    kj::UnixEventPort::captureChildExit();

    signal(SIGINT, &laminar_quit);
    signal(SIGTERM, &laminar_quit);

    printf("laminard version %s started\n", laminar_version());

    server->start();

    delete laminar;
    delete server;

    LLOG(INFO, "Clean exit");
    return 0;
}
