#!/bin/bash
mkdir -p out
rm -f out/nsgbe
# -DCOMPILE_TIME="`date -u +'%Y-%m-%d %H:%M:%S UTC'`"
clang \
    -Ofast \
    -march=native \
    -Wno-everything \
    `pkg-config --cflags gtk+-3.0` \
    -DGIT_HASH="\"`git rev-parse --short HEAD`\"" \
    -DGIT_BRANCH="\"`git rev-parse --abbrev-ref HEAD`\"" \
    emu/main.c \
    emu/nsgbe.c \
    emu/clock.c \
    emu/cpu.c \
    emu/io.c \
    emu/memory.c \
    emu/display.c \
    emu/ext_chip.c \
    emu/ext_chip/mbc1.c \
    emu/ext_chip/mbc3.c \
    emu/ext_chip/mbc5.c \
    app/window.c \
    `pkg-config --libs gtk+-3.0` \
    -o out/nsgbe
