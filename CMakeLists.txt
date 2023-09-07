###
### Copyright 2015-2021 Oliver Giles
###
### This file is part of Laminar
###
### Laminar is free software: you can redistribute it and/or modify
### it under the terms of the GNU General Public License as published by
### the Free Software Foundation, either version 3 of the License, or
### (at your option) any later version.
###
### Laminar is distributed in the hope that it will be useful,
### but WITHOUT ANY WARRANTY; without even the implied warranty of
### MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
### GNU General Public License for more details.
###
### You should have received a copy of the GNU General Public License
### along with Laminar.  If not, see <http://www.gnu.org/licenses/>
###
cmake_minimum_required(VERSION 3.6)
project(laminar)

if (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    # ld.lld is a default option on FreeBSD
    set(LLVM_LINKER_IS_LLD ON)
endif()

# ld.lld specific options. There is no sane way in cmake
# to detect if toolchain is actually using ld.lld
if (LLVM_LINKER_IS_LLD)
    if (NOT DEFINED LINKER_EMULATION_FLAGS)
        if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "amd64")
            set(LINKER_EMULATION_FLAGS "-melf_x86_64")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
            set(LINKER_EMULATION_FLAGS "-melf_x86_64")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
            set(LINKER_EMULATION_FLAGS "-maarch64elf")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "powerpc64le")
            set(LINKER_EMULATION_FLAGS "-melf64lppc")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "powerpc64")
            set(LINKER_EMULATION_FLAGS "-melf64ppc")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "riscv64")
            # llvm17 & riscv64 requires extra step, it is necessary to
            # patch 'Elf64.e_flags' (48-th byte) in binary-blob object files
            # with value 0x5 - to change soft_float ABI to hard_float ABI
            # so they can link with rest of the object files.
            set(LINKER_EMULATION_FLAGS "-melf64lriscv")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm")
            set(LINKER_EMULATION_FLAGS "-marmelf")
        elseif (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "armv7")
            set(LINKER_EMULATION_FLAGS "-marmelf")
        else()
            message(FATAL_ERROR
                "Unsupported '${CMAKE_SYSTEM_PROCESSOR}' translation to emulation flag. "
                "Please set it explicitly 'cmake -DLINKER_EMULATION_FLAGS=\"-melf_your_arch\" ...'")
        endif()
    endif()
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Werror -DDEBUG")

# Allow passing in the version string, for e.g. patched/packaged versions
if(NOT LAMINAR_VERSION AND EXISTS ${CMAKE_SOURCE_DIR}/.git)
    execute_process(COMMAND git describe --tags --abbrev=8 --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE LAMINAR_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT LAMINAR_VERSION)
    set(LAMINAR_VERSION xx-unversioned)
endif()
set_source_files_properties(src/version.cpp PROPERTIES COMPILE_DEFINITIONS
	LAMINAR_VERSION=${LAMINAR_VERSION})

# This macro takes a list of files, gzips them and converts the output into
# object files so they can be linked directly into the application.
# ld generates symbols based on the string argument given to its executable,
# so it is significant from which directory it is called. BASEDIR will be
# removed from the beginning of paths to the remaining arguments
macro(generate_compressed_bins BASEDIR)
    foreach(FILE ${ARGN})
        set(COMPRESSED_FILE "${FILE}.z")
        set(OUTPUT_FILE "${FILE}.o")
        get_filename_component(DIR ${FILE} PATH)
        if(DIR)
            file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${DIR})
        endif()
        add_custom_command(OUTPUT ${COMPRESSED_FILE}
            COMMAND gzip < ${BASEDIR}/${FILE} > ${COMPRESSED_FILE}
            DEPENDS ${BASEDIR}/${FILE}
        )
        add_custom_command(OUTPUT ${OUTPUT_FILE}
            COMMAND ${CMAKE_LINKER} ${LINKER_EMULATION_FLAGS} -r -b binary -o ${OUTPUT_FILE} ${COMPRESSED_FILE}
            COMMAND ${CMAKE_OBJCOPY}
              --rename-section .data=.rodata.alloc,load,readonly,data,contents
              --add-section .note.GNU-stack=/dev/null
              --set-section-flags .note.GNU-stack=contents,readonly ${OUTPUT_FILE}
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${COMPRESSED_FILE}
        )
        list(APPEND COMPRESSED_BINS ${OUTPUT_FILE})
    endforeach()
endmacro()

# Generates Cap'n Proto interface from definition file
add_custom_command(OUTPUT laminar.capnp.c++ laminar.capnp.h
    COMMAND capnp compile -oc++:${CMAKE_BINARY_DIR}
    --src-prefix=${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/laminar.capnp
    DEPENDS src/laminar.capnp)

# Zip and compile statically served resources
generate_compressed_bins(${CMAKE_SOURCE_DIR}/src/resources index.html js/app.js
    style.css manifest.webmanifest favicon.ico favicon-152.png icon.png)

# The code that allows dynamic modifying of index.html requires knowing its original size
add_custom_command(OUTPUT index_html_size.h
    COMMAND sh -c '( echo -n "\\#define INDEX_HTML_UNCOMPRESSED_SIZE " && wc -c < "${CMAKE_SOURCE_DIR}/src/resources/index.html" ) > index_html_size.h'
    DEPENDS src/resources/index.html)

# Download 3rd-party frontend JS libs...
file(DOWNLOAD https://cdnjs.cloudflare.com/ajax/libs/vue/2.6.12/vue.min.js
    ${CMAKE_BINARY_DIR}/js/vue.min.js EXPECTED_MD5 fb192338844efe86ec759a40152fcb8e)
file(DOWNLOAD https://raw.githubusercontent.com/drudru/ansi_up/v4.0.4/ansi_up.js
    ${CMAKE_BINARY_DIR}/js/ansi_up.js EXPECTED_MD5 b31968e1a8fed0fa82305e978161f7f5)
file(DOWNLOAD https://cdnjs.cloudflare.com/ajax/libs/Chart.js/3.9.1/chart.min.js
    ${CMAKE_BINARY_DIR}/js/Chart.min.js EXPECTED_MD5 7dd5ea7d2cf22a1c42b43c40093d2669)
# ...and compile them
generate_compressed_bins(${CMAKE_BINARY_DIR} js/vue.min.js
    js/ansi_up.js js/Chart.min.js)
# (see resources.cpp where these are fetched)

set(LAMINARD_CORE_SOURCES
    src/conf.cpp
    src/database.cpp
    src/laminar.cpp
    src/leader.cpp
    src/http.cpp
    src/resources.cpp
    src/rpc.cpp
    src/run.cpp
    src/server.cpp
    src/version.cpp
    laminar.capnp.c++
    index_html_size.h
)

find_package(CapnProto REQUIRED)
include_directories(${CAPNP_INCLUDE_DIRS})

find_package(SQLite3 REQUIRED)
include_directories(${SQLite3_INCLUDE_DIRS})

find_package(ZLIB REQUIRED)
include_directories(${ZLIB_INCLUDE_DIRS})

find_package(Threads REQUIRED)
include_directories(${Threads_INCLUDE_DIRS})

## Server
add_executable(laminard ${LAMINARD_CORE_SOURCES} src/main.cpp ${COMPRESSED_BINS})
target_link_libraries(laminard CapnProto::capnp-rpc CapnProto::capnp CapnProto::kj-http CapnProto::kj-async 
                               CapnProto::kj Threads::Threads SQLite::SQLite3 ZLIB::ZLIB)

if (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
pkg_check_modules(INOTIFY REQUIRED libinotify)
target_link_libraries(laminard ${INOTIFY_LINK_LIBRARIES})
endif()

## Client
add_executable(laminarc src/client.cpp src/version.cpp laminar.capnp.c++)
target_link_libraries(laminarc CapnProto::capnp-rpc CapnProto::capnp CapnProto::kj-async CapnProto::kj Threads::Threads)

## Manpages
macro(gzip SOURCE)
    get_filename_component(OUT_FILE ${SOURCE} NAME)
    add_custom_command(OUTPUT ${OUT_FILE}.gz
        COMMAND gzip < ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE} > ${OUT_FILE}.gz
        DEPENDS ${SOURCE})
endmacro()
add_custom_target(laminar-manpages ALL DEPENDS laminard.8.gz laminarc.1.gz)
gzip(etc/laminard.8)
gzip(etc/laminarc.1)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/laminard.8.gz DESTINATION share/man/man8)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/laminarc.1.gz DESTINATION share/man/man1)

## Tests
set(BUILD_TESTS FALSE CACHE BOOL "Build tests")
if(BUILD_TESTS)
    find_package(GTest REQUIRED)
    include_directories(${GTEST_INCLUDE_DIRS} src)
    add_executable(laminar-tests ${LAMINARD_CORE_SOURCES} ${COMPRESSED_BINS} test/main.cpp test/laminar-functional.cpp test/unit-conf.cpp test/unit-database.cpp)
    target_link_libraries(laminar-tests ${GTEST_LIBRARIES} capnp-rpc capnp kj-http kj-async kj pthread sqlite3 z)
endif()

set(SYSTEMD_UNITDIR /lib/systemd/system CACHE PATH "Path to systemd unit files")
set(BASH_COMPLETIONS_DIR /usr/share/bash-completion/completions CACHE PATH "Path to bash completions directory")
set(ZSH_COMPLETIONS_DIR /usr/share/zsh/site-functions CACHE PATH "Path to zsh completions directory")
install(TARGETS laminard RUNTIME DESTINATION sbin)
install(TARGETS laminarc RUNTIME DESTINATION bin)
install(FILES etc/laminarc-completion.bash DESTINATION ${CMAKE_INSTALL_PREFIX}${BASH_COMPLETIONS_DIR} RENAME laminarc)
install(FILES etc/laminarc-completion.zsh DESTINATION ${CMAKE_INSTALL_PREFIX}${ZSH_COMPLETIONS_DIR} RENAME _laminarc)

if (NOT ${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
    install(FILES etc/laminar.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/etc)
    configure_file(etc/laminar.service.in laminar.service @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/laminar.service DESTINATION ${CMAKE_INSTALL_PREFIX}${SYSTEMD_UNITDIR})
endif()