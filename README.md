# nsGBE
**N**o **S**pecial **G**ame **B**oy **E**mulator is a work-in-progress interpreting Game Boy emulator written in C.  
Its feature set is limited but it is able to run a good amount of original Game Boy (DMG) games that use no MBC or MBC3/5.  
Support for Game Boy Color games is planned but has not been implemented thus far. Development generally favors 
compatibility over accuracy and performance.

## Building

Currently, the project provides gui implementations based on GTK+ 3.0 (Cairo) and SDL2 targeting Linux.  
Windows and macOS are not officially supported, but the SDL2 flavor should work on those with some minor reconfiguration.

Run `$ ./configure-gtkplus` to generate build files for GTK+ using CMake.  
Alternatively, run `$ ./configure-sdl2` to generate build files for SDL2.  
Then, run `$ ./build` to compile. This will produce `nsgbe` in `out/`.

**Note:** To use Clang instead of your default C/C++ compiler (likely GCC if you're on Linux), run `$ export CC=/usr/bin/clang` and `$ export CXX=/usr/bin/clang++` (adjust paths if necessary) prior to executing the `configure-*` script.

## Usage

Launch the program by running `$ nsgbe {path/to/rom.gb}`  
Make sure the directory containing your rom file is writable if you wish to be able to save the battery (savegame) upon quitting.

Joypad keys are hardcoded right now. They're mapped as follows:

| Keyboard key | Console key |
| --- | --- |
| W | Up |
| A | Left |
| S | Down |
| D | Right |
| K | A |
| O | B |
| L | Start |
| P | Select |
| Spacebar (hold) | Overclock x4 |

You can customize these to your liking in `app/*/window.c`.

## Test Rom Results

![blargg's cpu instruction tests](res/cpu_instrs.png)
