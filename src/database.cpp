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
#include "database.h"

#include <sqlite3.h>
#include <string.h>

Database::Database(const char *path) {
    sqlite3_open(path, &hdl);
}

Database::~Database() {
    sqlite3_close(hdl);
}

Database::Statement::Statement(sqlite3 *db, const char *query) {
    sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
}

Database::Statement::~Statement() {
    sqlite3_finalize(stmt);
}


bool Database::Statement::exec() {
    return sqlite3_step(stmt) == SQLITE_OK;
}

void Database::Statement::bindValue(int i, int e) {
    sqlite3_bind_int(stmt, i, e);
}

void Database::Statement::bindValue(int i, const char* e) {
    sqlite3_bind_text(stmt, i, e, -1, NULL);
}

void Database::Statement::bindValue(int i, const std::string& e) {
    sqlite3_bind_text(stmt, i, e.data(), e.size(), NULL);
}

template<> std::string Database::Statement::fetchColumn(int col) {
    int sz = sqlite3_column_bytes(stmt, col);
    std::string res(sz, '\0');
    memcpy(&res[0], sqlite3_column_text(stmt, col), sz);
    return res;
}

template<> const char* Database::Statement::fetchColumn(int col) {
    return (char*)sqlite3_column_text(stmt, col);
}

template<> int Database::Statement::fetchColumn(int col) {
    return sqlite3_column_int(stmt, col);
}

template<> time_t Database::Statement::fetchColumn(int col) {
    return sqlite3_column_int64(stmt, col);
}

bool Database::Statement::row() {
    return sqlite3_step(stmt) == SQLITE_ROW;
}
