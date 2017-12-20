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
#ifndef LAMINAR_LOG_H_
#define LAMINAR_LOG_H_

#include <kj/debug.h>

// Simple override to prevent full paths to source files from
// appearing in log messages. Assumes / is the path separator.
// @see kj/debug.h

#define _LBASENAME(path) strrchr(path, '/') ? strrchr(path, '/') + 1 : path

#define LLOG(severity, ...) \
  if (!::kj::_::Debug::shouldLog(::kj::_::Debug::Severity::severity)) {} else \
    ::kj::_::Debug::log(_LBASENAME(__FILE__), __LINE__, ::kj::_::Debug::Severity::severity, \
                        #__VA_ARGS__, __VA_ARGS__)


#endif // LAMINAR_LOG_H_

