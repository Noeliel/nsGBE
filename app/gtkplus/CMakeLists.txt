# SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
#
# SPDX-License-Identifier: LGPL-2.0-only

cmake_minimum_required(VERSION 3.10.2)

project("nsgbe")

find_package(Git)
if(Git_FOUND)
  execute_process(COMMAND
    "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE CMAKE_GIT_HASH
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

  execute_process(COMMAND
    "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE CMAKE_GIT_BRANCH
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)

  add_compile_definitions(GIT_HASH=\"${CMAKE_GIT_HASH}\" GIT_BRANCH=\"${CMAKE_GIT_BRANCH}\")
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "-march=native -w")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-Ofast")

set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_WIN32_THREADS_INIT 0)
set(CMAKE_USE_PTHREADS_INIT 1)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKPLUS REQUIRED IMPORTED_TARGET gtk+-3.0)

add_executable(
    ${PROJECT_NAME}
    main.c
    window.c
    ../../emu/nsgbe.c
    ../../emu/clock.c
    ../../emu/cpu.c
    ../../emu/io.c
    ../../emu/memory.c
    ../../emu/display.c
    ../../emu/ext_chip.c
    ../../emu/ext_chip/mbc1.c
    ../../emu/ext_chip/mbc3.c
    ../../emu/ext_chip/mbc5.c
)

target_include_directories(
    ${PROJECT_NAME} PUBLIC
    PkgConfig::GTKPLUS
)

target_link_libraries(
    ${PROJECT_NAME} PUBLIC
    Threads::Threads
    PkgConfig::GTKPLUS
)
