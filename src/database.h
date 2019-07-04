///
/// Copyright 2015-2018 Oliver Giles
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
#ifndef LAMINAR_DATABASE_H_
#define LAMINAR_DATABASE_H_

#include <string>
#include <functional>

// Definition needed for musl
typedef unsigned int uint;
typedef unsigned long ulong;

struct sqlite3;
struct sqlite3_stmt;

// This is a small sqlite wrapper using some clever template action
// to somewhat reduce verbosity. Usage:
//   db.stmt("SELECT result WHERE name = ?")
//     .bind(name)
//     .fetch([](int result) {
//          // function called for each retrieved row
//          doSomething(result);
//      });
class Database {
public:
    Database(const char* path);
    ~Database();

private:
    // Represents a database statement. Call Database::stmt() to get
    // one, then call bind(), fetch() or exec() on the returned object
    class Statement {
    private:
        // Internal template helper that defines the type
        // in the variadic type array Args at offset N
        template<int N, typename T, typename...Args>
        struct typeindex : typeindex<N-1, Args...> {};
        template<typename T, typename...Args>
        struct typeindex<0, T, Args...> { typedef T type; };

    public:
        Statement(sqlite3* db, const char* query);
        Statement(const Statement&) =delete;
        Statement(Statement&& other) {
            stmt = other.stmt;
            other.stmt = nullptr;
        }
        ~Statement();

        // Bind several parameters in a single call. They are bound
        // by index in the order passed into this function. Must be
        // passed by reference because arguments may be std::strings,
        // which must be passed by reference because sqlite requires
        // the bound string's lifetime to exist until sqlite3_step
        template<typename...Args>
        Statement& bind(const Args&...args) {
            return bindRecursive<Args...>(1, args...);
        }
        // Fetch columns. Supply a callback that will be executed for
        // each row in the resultset, with arguments matching the
        // expected column types
        template<typename...Args>
        void fetch(typename typeindex<0, std::function<void(Args...)>>::type callback) {
            FetchMarshaller<Args...> fm(this, callback);
        }
        // execute without fetching any parameters. Intended for
        // non-SELECT statements;
        bool exec();

    private:
        // Internal template helper used to unpack arguments into
        // the fetch callback.
        template<int...N> struct rng { };
        // Internal template helper to generate a rng<> object:
        // genrng<4>::type is rng<0,1,2,3>
        template<int J, int...N>
        struct genrng : genrng<J-1, J-1, N...> {};
        template<int...N>
        struct genrng<0, N...> { typedef rng<N...> type; };

        template<typename...Args>
        struct FetchMarshaller {
            FetchMarshaller(Statement* st, std::function<void(Args...)> cb){
                marshal(st, cb, typename genrng<sizeof...(Args)>::type());
            }
            template<int...N>
            void marshal(Statement* st, std::function<void(Args...)> cb, rng<N...>) {
                while(st->row()) {
                    cb(st->fetchColumn<typename typeindex<N, Args...>::type>(N)...);
                }
            }
        };
        template<typename...Args>
        friend struct FetchMarshaller;

        bool row();

        template<typename T, typename...Args>
        Statement& bindRecursive(int i, const T& v, const Args&...args) {
            bindValue(i, v); // specialization must exist for T
            return bindRecursive(i + 1, args...);
        }
        // template terminating condition
        Statement& bindRecursive(int) {
            return *this;
        }

        // Bind value specializations
        void bindValue(int i, int e);
        void bindValue(int i, uint e);
        void bindValue(int i, long e);
        void bindValue(int i, unsigned long e);
        void bindValue(int i, const char* e);
        void bindValue(int i, const std::string& e);

        // Declaration for fetch column interface,
        // intentionally missing definition
        template<typename T>
        T fetchColumn(int col);

        sqlite3_stmt* stmt;
    };

public:
    Statement stmt(const char* q) {
        return Statement(hdl, q);
    }
    // shorthand
    bool exec(const char* q) { return Statement(hdl, q).exec(); }
private:

    sqlite3* hdl;
};

// specialization declarations, defined in source file
template<> std::string Database::Statement::fetchColumn(int col);
template<> const char* Database::Statement::fetchColumn(int col);
template<> int Database::Statement::fetchColumn(int col);
template<> uint Database::Statement::fetchColumn(int col);
template<> long Database::Statement::fetchColumn(int col);
template<> ulong Database::Statement::fetchColumn(int col);
template<> double Database::Statement::fetchColumn(int col);

#endif // LAMINAR_DATABASE_H_
