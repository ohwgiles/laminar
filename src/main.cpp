///
/// Copyright 2015-2016 Oliver Giles
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
#include "log.h"
#include <signal.h>
#include <kj/async-unix.h>
#include <kj/filesystem.h>

static Laminar* laminar;

static void laminar_quit(int) {
    laminar->stop();
}

int main(int argc, char** argv) {
    for(int i = 1; i < argc; ++i) {
        if(strcmp(argv[i], "-v") == 0) {
            kj::_::Debug::setLogLevel(kj::_::Debug::Severity::INFO);
        }
    }

    laminar = new Laminar(getenv("LAMINAR_HOME") ?: "/var/lib/laminar");
    kj::UnixEventPort::captureChildExit();

    signal(SIGINT, &laminar_quit);
    signal(SIGTERM, &laminar_quit);

    laminar->run();

    delete laminar;

    LLOG(INFO, "Clean exit");
    return 0;
}
