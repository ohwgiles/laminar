///
/// Copyright 2018-2022 Oliver Giles
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
#ifndef LAMINAR_TEMPDIR_H_
#define LAMINAR_TEMPDIR_H_

#include "log.h"

#include <kj/filesystem.h>
#include <stdlib.h>

class TempDir {
public:
    TempDir() :
        path(mkdtemp()),
        fs(kj::newDiskFilesystem()->getRoot().openSubdir(path, kj::WriteMode::MODIFY))
    {
    }
    ~TempDir() noexcept {
        kj::newDiskFilesystem()->getRoot().remove(path);
    }
    void init() {
        // set up empty directory structure
        fs->openSubdir(kj::Path{"cfg"}, kj::WriteMode::CREATE);
        fs->openSubdir(kj::Path{"cfg", "jobs"}, kj::WriteMode::CREATE);
        fs->openSubdir(kj::Path{"cfg", "contexts"}, kj::WriteMode::CREATE);
    }
    void clean() {
        // rm -rf in config folder
        for(kj::StringPtr name : fs->listNames()) {
            fs->remove(kj::Path{name});
        }
    }

    kj::Path path;
    kj::Own<const kj::Directory> fs;
private:
    static kj::Path mkdtemp() {
        char dir[] = "/tmp/laminar-test-XXXXXX";
        LASSERT(::mkdtemp(dir) != nullptr, "mkdtemp failed");
        return kj::Path::parse(&dir[1]);
    }
};

#endif // LAMINAR_TEMPDIR_H_
