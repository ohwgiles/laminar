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
#ifndef LAMINAR_CONTEXT_H_
#define LAMINAR_CONTEXT_H_

#include <string>
#include <set>
class Run;

// Represents a context within which a Run will be executed. Allows applying
// a certain environment to a set of Jobs, or setting a limit on the number
// of parallel Runs
class Context {
public:
    Context() {}

    std::string name;
    int numExecutors;
    int busyExecutors = 0;
    std::set<std::string> jobPatterns;
};


#endif // LAMINAR_CONTEXT_H_
