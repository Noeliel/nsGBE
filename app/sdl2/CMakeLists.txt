cmake_minimum_required(VERSION 3.10.2)

project("nsgbe")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "-march=native -w")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-Ofast")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

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
    PkgConfig::SDL2
)

target_link_libraries(
    ${PROJECT_NAME} PUBLIC
    PkgConfig::SDL2
)
