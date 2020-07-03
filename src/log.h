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
#ifndef LAMINAR_LOG_H_
#define LAMINAR_LOG_H_

#include <kj/debug.h>
#include <utility>

// Prevent full file paths from appearing in log messages. With compiler
// optimization, this compile-time method should completely prevent the
// paths from being encoded into the binary at all. Assumes / is the
// path separator.
namespace _ {
    constexpr const char* static_basename_impl(const char* b, const char* t) {
        return *t == '\0' ? b : static_basename_impl(*t == '/' ? t+1 : b, t+1);
    }
    constexpr const char* static_basename(const char* p) {
        return static_basename_impl(p, p);
    }
    constexpr int static_strlen(const char* s) {
        return *s == '\0' ? 0 : static_strlen(s + 1) + 1;
    }
    template<int N, int...I>
    static constexpr decltype(auto) static_alloc_str_impl(const char* str, std::integer_sequence<int, I...>) {
        typedef struct {char buf[N+1];} static_null_terminated;
        return (static_null_terminated) {str[I]..., '\0'};
    }
    template<int N>
    static constexpr decltype(auto) static_alloc_str(const char* str) {
        return static_alloc_str_impl<N>(str, std::make_integer_sequence<int, N>());
    }
}
#define __FILE_BASE__ (::_::static_alloc_str<::_::static_strlen(::_::static_basename(__FILE__))>\
                        (::_::static_basename(__FILE__)).buf)

// Provide alternative implementations to those from kj/debug.h which
// use __FILE__ directly and thus cause the full path to be encoded in
// the final binary
#define LLOG(severity, ...) \
  if (!::kj::_::Debug::shouldLog(::kj::_::Debug::Severity::severity)) {} else \
    ::kj::_::Debug::log(__FILE_BASE__, __LINE__, \
      ::kj::_::Debug::Severity::severity, #__VA_ARGS__, __VA_ARGS__)

#define LASSERT(cond, ...) \
  if (KJ_LIKELY(cond)) {} else \
    for (::kj::_::Debug::Fault f(__FILE_BASE__, __LINE__, \
      ::kj::Exception::Type::FAILED, #cond, #__VA_ARGS__, ##__VA_ARGS__);; f.fatal())

#define LSYSCALL(call, ...) \
  if (auto _kjSyscallResult = ::kj::_::Debug::syscall([&](){return (call);}, false)) {} else \
    for (::kj::_::Debug::Fault f(__FILE_BASE__, __LINE__, \
      _kjSyscallResult.getErrorNumber(), #call, #__VA_ARGS__, ##__VA_ARGS__);; f.fatal())

const char* laminar_version();

#endif // LAMINAR_LOG_H_

