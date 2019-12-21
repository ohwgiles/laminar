///
/// Copyright 2019 Oliver Giles
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
#ifndef LAMINAR_LEADER_H_
#define LAMINAR_LEADER_H_

// Main function for the leader process which is responsible for
// executing all the scripts which make up a Run. Separating this
// into its own process allows for a cleaner process tree view,
// where it's obvious which script belongs to which run of which
// job, and allows this leader process to act as a subreaper for
// any wayward child processes.

// This could have been implemented as a separate process, but
// instead we just fork & exec /proc/self/exe from the main laminar
// daemon, and distinguish based on argv[0]. This saves installing
// another binary and avoids some associated pitfalls.

int leader_main(void);

#endif // LAMINAR_LEADER_H_
