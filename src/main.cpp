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
#include "laminar.h"

#include <signal.h>
#include <kj/debug.h>

std::function<void()> sigHandler;
static void __sigHandler(int) { sigHandler(); }

int main(int argc, char** argv) {
    for(int i = 1; i < argc; ++i) {
        if(strcmp(argv[i], "-v") == 0) {
            kj::_::Debug::setLogLevel(kj::_::Debug::Severity::INFO);
        }
    }

    const char* configFile = getenv("LAMINAR_CONF_FILE");
    if(!configFile || !*configFile)
        configFile = "/etc/laminar.conf";

    do {
        Laminar laminar(configFile);
        sigHandler = [&](){
            KJ_LOG(INFO, "Received SIGINT");
            laminar.stop();
        };
        signal(SIGINT, &__sigHandler);
        signal(SIGTERM, &__sigHandler);

        laminar.run();
    } while(false);

    KJ_DBG("end of main");
    return 0;
}

