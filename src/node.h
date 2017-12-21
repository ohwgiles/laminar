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
#ifndef LAMINAR_NODE_H_
#define LAMINAR_NODE_H_

#include <string>
#include <set>

class Run;

// Represents a group of executors. Currently almost unnecessary POD
// abstraction, but may be enhanced in the future to support e.g. tags
class Node {
public:
    Node() {}

    std::string name;
    int numExecutors;
    int busyExecutors = 0;
    std::set<std::string> tags;

    // Attempts to queue the given run to this node. Returns true if succeeded.
    bool queue(const Run& run);
};


#endif // LAMINAR_NODE_H_
