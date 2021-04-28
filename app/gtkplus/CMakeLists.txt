cmake_minimum_required(VERSION 3.10.2)

project("nsgbe")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "-march=native -w")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-Ofast")

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
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
    ../../emu
    PkgConfig::GTKPLUS
)

target_link_libraries(
    ${PROJECT_NAME} PUBLIC
    Threads::Threads
    PkgConfig::GTKPLUS
)
