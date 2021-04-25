/**
    N(o) S(pecial) G(ame) B(oy) E(mulator)
    Copyright (C) 2021  Noeliel

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**/


#include <stdint.h>

#define GB_FRAMEBUFFER_WIDTH 160
#define GB_FRAMEBUFFER_HEIGHT 144

// this is a high level representation used for state transfer
// between the platform-dependend api (gtk+ for example) and
// emulation i/o
union BUTTON_STATE {
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        _Bool A      : 1; // action button
        _Bool B      : 1; // action button
        _Bool START  : 1; // action button
        _Bool SELECT : 1; // action button
        _Bool UP     : 1; // direction button
        _Bool DOWN   : 1; // direction button
        _Bool LEFT   : 1; // direction button
        _Bool RIGHT  : 1; // direction button
#else
        _Bool RIGHT  : 1;
        _Bool LEFT   : 1;
        _Bool DOWN   : 1;
        _Bool UP     : 1;
        _Bool SELECT : 1;
        _Bool START  : 1;
        _Bool B      : 1;
        _Bool A      : 1;
#endif
    };
    uint8_t b;
};

/*------------platform-specific gui------------*/

// copy of button states owned and modified by frontend
extern union BUTTON_STATE button_states;

// any frontend or gui provides implementations for these
extern long load_rom(uint8_t **buffer);
extern long load_bios(uint8_t **buffer);
extern long load_battery(uint8_t **buffer);
extern int save_battery(uint8_t *buffer, size_t size);

/*--------------------NSGBE----------------------*/

// frontend can toggle system overclock by changing this bool
extern _Bool system_overclock;

// frontend uses these to interact with the core
extern void system_run();
extern void write_battery();
extern uint8_t *display_request_next_frame();

// frontend can set up a callback on this to be notified about new frames
extern void (* display_notify_vblank)();
