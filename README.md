# nsGBE
**N**o **S**pecial **G**ame **B**oy **E**mulator is a work-in-progress interpreting Game Boy (Color) emulator written in C.
Its feature set is limited but it is able to run a good amount of original Game Boy (DMG) and some Game Boy Color (CGB, experimental) games that use no MBC or MBC3/5.
APU / audio support is missing and has low priority.
Development generally favors compatibility over accuracy and performance.

## Give it a try

You can find browser-based versions of nsGBE hosted on my website.
[WebAssembly variant](https://noeliel.com/nsGBE/wasm/) (better performance but requires your browser to support WebAssembly.)
[JavaScript variant](https://noeliel.com/nsGBE/js/) (better browser compatibility but worse performance.)

Please note that you will have to supply your own ROM files.
Also, this application runs entirely on your local machine in your browser, so no files are uploaded to any server at any point in time.

## Building (native)

Currently, the project provides gui implementations based on GTK+ 3.0 (Cairo) and SDL2 targeting Linux.
Windows and macOS are not officially supported, but the browser-based variant works on those.

Run `$ ./configure-gtkplus` to generate build files for GTK+ using CMake.
Alternatively, run `$ ./configure-sdl2` to generate build files for SDL2.
Then, run `$ ./build` to compile. This will produce `nsgbe` in `out/`.

**Note:** To use Clang instead of your default C/C++ compiler (likely GCC if you're on Linux), run `$ export CC=/usr/bin/clang` and `$ export CXX=/usr/bin/clang++` (adjust paths if necessary) prior to executing the `configure-*` script.

## Usage

Launch the program by running `$ nsgbe {path/to/rom.*}`
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
