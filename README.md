# nsgbe
**N**o **S**pecial **G**ame **B**oy **E**mulator is a work-in-progress interpreting Game Boy emulator written in C.  
Its feature set is limited but it is able to run a good amount of original Game Boy (DMG) games that use no MBC or MBC3/5.  
Support for Game Boy Color games is planned but has not been implemented thus far. Development generally favors 
compatibility over accuracy and performance.

## Building

Currently, the project only contains a gui for Linux based on gtk+3.0 / Cairo.  
Windows and macOS are not (yet?) supported.

Run `$ build-clang.sh` or `$ build-gcc.sh` to compile the source code into a binary.  
Makefiles are planned for the near future.

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

You can customize these to your liking in `app/window.c`.
