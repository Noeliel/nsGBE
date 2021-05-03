cmake_minimum_required(VERSION 3.10.2)

project("nsgbe")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS "-march=native -w -s WASM=0 -s LINKABLE=1 -s EXPORT_ALL=1 -s USE_SDL=2 -lidbfs.js")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-O3")

add_executable(
    ${PROJECT_NAME}.js
    ../main.c
    ../window.c
    ../../../emu/nsgbe.c
    ../../../emu/clock.c
    ../../../emu/cpu.c
    ../../../emu/io.c
    ../../../emu/memory.c
    ../../../emu/display.c
    ../../../emu/ext_chip.c
    ../../../emu/ext_chip/mbc1.c
    ../../../emu/ext_chip/mbc3.c
    ../../../emu/ext_chip/mbc5.c
)

target_include_directories(
    ${PROJECT_NAME}.js PUBLIC
    ../../../emu
)

target_link_libraries(
    ${PROJECT_NAME}.js PUBLIC
)
