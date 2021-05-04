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


#ifndef nsGBE_env_h
#define nsGBE_env_h

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <nsgbe.h>

/*---------------------ENV-----------------------*/

//#define __DEBUG 1
#ifdef __DEBUG
#define DEBUG_PRINT(x) (printf x)
#else
#define DEBUG_PRINT(x) ( NULL )
#endif

typedef uint8_t byte;

#ifndef __LITTLE_ENDIAN__
// try to use endian.h
// #if __BYTE_ORDER == __LITTLE_ENDIAN
// #define __LITTLE_ENDIAN__ 1
// #endif
#define GGC_LITTLE_ENDIAN   0x41424344UL
#define GGC_BIG_ENDIAN      0x44434241UL
#define GGC_PDP_ENDIAN      0x42414443UL
#define GGC_ENDIAN_ORDER    ('ABCD')
#if GGC_ENDIAN_ORDER == GGC_LITTLE_ENDIAN
#define __LITTLE_ENDIAN__ 1
#endif
#endif

typedef union {
    uint16_t w;

    struct {
#ifdef __LITTLE_ENDIAN__
        byte l, h;
#else
        byte h, l;
#endif
    } b;
} word;

#define word(value) ((word)(uint16_t)(value))

extern void free_ptr(void **ptr);

/*--------------------MISC--------------------*/

#define PREFER_CGB_MODE 1

typedef enum GB_MODE {
    MODE_DMG,
    MODE_CGB
};

extern enum GB_MODE gb_mode;

extern void *rombuffer;
extern uintptr_t romsize;

extern void *biosbuffer;
extern uintptr_t biossize;

extern void battery_load(byte **battery_banks, uint16_t bank_count);

/*--------------------CLOCK----------------------*/

#define USEC_PER_SEC 1000000

// #define MACHINE_CLOCK_HZ             1053360 // 60 fps
#define MACHINE_CLOCK_HZ                1048576 // original DMG
#define CPU_TICKS_PER_MACHINE_CLOCK     4
#define PPU_TICKS_PER_MACHINE_CLOCK     CPU_TICKS_PER_MACHINE_CLOCK     // currently ticking at cpu rate
#define RAM_TICKS_PER_MACHINE_CLOCK     4
#define VRAM_TICKS_PER_MACHINE_CLOCK    2
#define IO_TICKS_PER_MACHINE_CLOCK      CPU_TICKS_PER_MACHINE_CLOCK     // currently ticking at cpu rate

extern void clock_loop();

/*---------------------CPU-----------------------*/

#define FLAG_CARRY    (1 << 4)
#define FLAG_HCARRY   (1 << 5)
#define FLAG_SUBTRACT (1 << 6)
#define FLAG_ZERO     (1 << 7)

union CPU_FLAG { // apparently bitfield order is a thing, usually tied to endianess (compiler handles it though)
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        byte unused : 4;
        _Bool C : 1; // carry
        _Bool H : 1; // half carry
        _Bool N : 1; // subtract
        _Bool Z : 1; // zero
#else
        _Bool Z : 1;
        _Bool N : 1;
        _Bool H : 1;
        _Bool C : 1;
        byte unused : 4;
#endif
    };
    byte b;
};

struct CPU_REGS {
    union {
        struct {
#ifdef __LITTLE_ENDIAN__
            union CPU_FLAG F;
            byte A;
#else
            byte A;
            union CPU_FLAG F;
#endif
        };
        uint16_t AF;
    };

    union {
        struct {
#ifdef __LITTLE_ENDIAN__
            byte C;
            byte B;
#else
            byte B;
            byte C;
#endif
        };
        uint16_t BC;
    };

    union {
        struct {
#ifdef __LITTLE_ENDIAN__
            byte E;
            byte D;
#else
            byte D;
            byte E;
#endif
        };
        uint16_t DE;
    };

    union {
        struct {
#ifdef __LITTLE_ENDIAN__
            byte L;
            byte H;
#else
            byte H;
            byte L;
#endif
        };
        uint16_t HL;
    };

    uint16_t PC;
    uint16_t SP;
};

extern struct CPU_INSTRUCTION;

extern struct CPU_REGS cpu_regs;
extern _Bool cpu_alive;
extern _Bool cpu_dma_halt;
extern byte interrupt_master_enable;

extern void cpu_reset();
extern uint32_t cpu_step();
extern int32_t cpu_exec_cycles(int32_t clock_cycles_to_execute);
extern void cpu_break();
extern struct CPU_INSTRUCTION *cpu_next_instruction();
extern void handle_interrupts();

extern void fake_dmg_bootrom();
extern void fake_cgb_bootrom();

/*---------------------MEMORY--------------------*/

union INTERRUPT_REG {
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        _Bool VBLANK   : 1;
        _Bool LCD_STAT : 1;
        _Bool TIMER    : 1;
        _Bool SERIAL   : 1;
        _Bool JOYPAD   : 1;
        byte unused    : 3;
#else
        byte unused    : 3;
        _Bool JOYPAD   : 1;
        _Bool SERIAL   : 1;
        _Bool TIMER    : 1;
        _Bool LCD_STAT : 1;
        _Bool VBLANK   : 1;
#endif
    };
    byte b;
};

union MEMORY {
    struct {
        /* perm   */ byte rom_bank[0x4000];                    // 16kB..0x0000 - 0x3FFF // cartridge mapped
        /* switch */ byte rom_bank_s[0x4000];                  // 16kB..0x4000 - 0x7FFF // switchable for roms > 32kB
        /* perm   */ byte video_ram[0x2000];                   //  8kB..0x8000 - 0x9FFF // VRAM
        /* switch */ byte cart_ram_bank_s[0x2000];             //  8kB..0xA000 - 0xBFFF // external ram (cartridge/built-in nvram, eg. used for savegames)
        /* perm   */ byte ram_bank_0[0x1000];                  //  4kB..0xC000 - 0xCFFF // internal ram (W(orking)RAM)
        /* perm   */ byte ram_bank_1[0x1000];                  //  4kB..0xD000 - 0xDFFF // internal ram (W(orking)RAM)
        /* perm   */ byte undefined[0x1E00];                   //  7kB..0xE000 - 0xFDFF // internal ram, mirror of 0xC000 - 0xDDFF (ram_bank_0 & ram_bank_1)
        /* perm   */ byte sprite_attr_table[0xA0];             // 160B..0xFE00 - 0xFE9F // internal ram
        /* perm   */ byte prohibited[0x60];                    //  96B..0xFEA0 - 0xFEFF // not usable
        /* perm   */ byte dev_maps1[0xF];                      //  14B..0xFF00 - 0xFF0E // fixed device maps 1
        /* perm   */ union INTERRUPT_REG interrupt_flag_reg;   //   1B..0xFF0F
        /* perm   */ byte dev_maps2[0x70];                     // 113B..0xFF10 - 0xFF7F // fixed device maps 2
        /* perm   */ byte high_ram[0x7F];                      // 127B..0xFF80 - 0xFFFE // special WRAM
        /* perm   */ union INTERRUPT_REG interrupt_enable_reg; //   1B..0xFFFF
    } map;
    byte raw[0x10000]; // 64kB
};

extern union MEMORY mem; // due to endianess & mapping you shouldn't access this directly; instead, use the 4 functions below

/* CGB stuff */

extern byte cgb_extra_vram_bank[0x2000];
extern byte cgb_extra_wram_banks[8][0x1000];

/* EOF CGB stuff */

extern void init_memory();
extern byte mem_read(uint16_t offset);
extern word mem_read_16(uint16_t offset);
extern void mem_write(uint16_t offset, byte data);
extern void mem_write_16(uint16_t offset, word data);

extern _Bool enable_bootrom;

/*-----------------------IO-----------------------*/

// gameboy joypad byte encoding
union JOYPAD_IO {
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        _Bool RA    : 1; // right (direction) or A (action)
        _Bool LB    : 1; // left (direction) or B (action)
        _Bool USEL  : 1; // up (direction) or SELECT (action)
        _Bool DSTR  : 1; // down (direction) or START (action)
        _Bool DIR   : 1; // interpret as direction
        _Bool ACT   : 1; // interpret as action
        byte unused : 2; // unused
#else
        byte unused : 2; // unused
        _Bool ACT   : 1; // interpret as action
        _Bool DIR   : 1; // interpret as direction
        _Bool DSTR  : 1; // down (direction) or START (action)
        _Bool USEL  : 1; // up (direction) or SELECT (action)
        _Bool LB    : 1; // left (direction) or B (action)
        _Bool RA    : 1; // right (direction) or A (action)
#endif
    };
    byte b;
};

union TIMER_CONTROL_IO {
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        byte clock   : 2;
        _Bool enable : 1;
        byte unused  : 5; // unused
#else
        byte unused  : 5; // unused
        _Bool enable : 1;
        byte clock   : 2;
#endif
    };
    byte b;
};

extern int32_t io_exec_cycles(int32_t clock_cycles_to_execute);
extern uint16_t io_interpret_read(uint16_t offset);
extern uint16_t io_interpret_write(uint16_t offset, byte data);

/*--------------------EXT_CHIP--------------------*/

extern uint16_t rom_bank_count;
extern uint16_t ext_ram_bank_count;
extern byte **rom_banks;
extern byte **ext_ram_banks;
extern word active_rom_bank;
extern word active_ext_ram_bank;

extern _Bool battery_enabled;

extern uint16_t (* active_mbc_writes_interpreter)(uint16_t offset, byte data);
extern uint16_t (* active_mbc_reads_interpreter)(uint16_t offset);

extern uint32_t mbc3_setup();
extern uint32_t mbc5_setup();

/*------------------DISPLAY/PPU--------------------*/

#define OAM     0xFE00 // (r/w) sprite attribute table
#define OAM_END 0xFE9F

#define LCDC 0xFF40 // (r/w) lcd control
#define STAT 0xFF41 // (r/w) lcd status

#define SCY  0xFF42 // (r/w) scroll y
#define SCX  0xFF43 // (r/w) scroll x

#define LY   0xFF44 // (ro) lcdc y-coord
#define LYC  0xFF45 // (r/w) ly compare

#define DMA  0xFF46 // (r/w) dma transfer and start address

#define BGP  0xFF47 // (r/w) bg palette data (not on gameboy color)
#define OBP0 0xFF48 // (r/w) object palette 0 data (not on gameboy color)
#define OBP1 0xFF49 // (r/w) object palette 1 data (not on gameboy color)

#define VBK 0xFF4F // (r/w) active vram bank (only on gameboy color)

#define HDMA1 0xFF51 // (w) new source, high (only on gameboy color)
#define HDMA2 0xFF52 // (w) new source, low (only on gameboy color)
#define HDMA3 0xFF53 // (w) new destination, high (only on gameboy color)
#define HDMA4 0xFF54 // (w) new destination, low (only on gameboy color)
#define HDMA5 0xFF55 // (w) new dma length/mode/start (only on gameboy color)

#define WY   0xFF4A // (r/w) window y position
#define WX   0xFF4B // (r/w) window x position + 7

#define BCPS 0xFF68 // (?) background color palette specification (only on gameboy color)
#define BCPD 0xFF69 // (?) background color palette data (only on gameboy color)
#define OCPS 0xFF6A // (?) object color palette specification (only on gameboy color)
#define OCPD 0xFF6B // (?) object color palette data (only on gameboy color)

#define PPU_HBLANK_MODE    0 // -> 1
#define PPU_VBLANK_MODE    1 // -> 2
#define PPU_OAM_READ_MODE  2 // -> 3
#define PPU_VRAM_READ_MODE 3 // -> 0

struct PPU_REGS {
    union PPU_LCDC {
        struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
            _Bool bg_window_enable_prio    : 1; // 0 = off, 1 = on
            _Bool obj_enable               : 1; // 0 = off, 1 = on
            _Bool obj_size                 : 1; // 0 = 8x8, 1 = 8x16
            _Bool bg_tile_map_area         : 1; // 0 = 9800-9BFF, 1 = 9C00-9FFF
            _Bool bg_window_tile_data_area : 1; // 0 = 8800-97FF, 1 = 8000-8FFF
            _Bool window_enable            : 1; // 0 = off, 1 = on
            _Bool window_tile_map_area     : 1; // 0 = 9800-9BFF, 1 = 9C00-9FFF
            _Bool lcd_ppu_enable           : 1; // 0 = off, 1 = on
#else
            _Bool lcd_ppu_enable           : 1;
            _Bool window_tile_map_area     : 1;
            _Bool window_enable            : 1;
            _Bool bg_window_tile_data_area : 1;
            _Bool bg_tile_map_area         : 1;
            _Bool obj_size                 : 1;
            _Bool obj_enable               : 1;
            _Bool bg_window_enable_prio    : 1;
#endif
        };
        byte b;
    } *lcdc;

    union PPU_STAT {
        struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
            byte mode           : 2; // 0 = PPU_HBLANK_MODE, 1 = PPU_VBLANK_MODE, 2 = PPU_OAM_READ_MODE, 3 = PPU_VRAM_READ_MODE
            _Bool lyc_eq_ly     : 1; // 0 = different, 1 = equal
            _Bool hblank_int    : 1; // 1 = enable
            _Bool vblank_int    : 1; // 1 = enable
            _Bool oam_int       : 1; // 1 = enable
            _Bool lyc_eq_ly_int : 1; // 1 = enable
            _Bool unused        : 1;
#else
            _Bool unused        : 1;
            _Bool lyc_eq_ly_int : 1;
            _Bool oam_int       : 1;
            _Bool vblank_int    : 1;
            _Bool hblank_int    : 1;
            _Bool lyc_eq_ly     : 1;
            byte mode           : 2;
#endif
        };
        byte b;
    } *stat;
};

extern struct PPU_REGS ppu_regs;
extern _Bool ppu_alive;

extern void ppu_reset();
extern void ppu_step();
extern int32_t ppu_exec_cycles(int32_t clock_cycles_to_execute);
extern void ppu_break();

extern uint16_t ppu_interpret_read(uint16_t offset);
extern uint16_t ppu_interpret_write(uint16_t offset, byte data);

/*---------------------NOTES----------------------*/

/*
 CPU Flag register (F) bits
 +-+-+-+-+-+-+-+-+
 |7|6|5|4|3|2|1|0|
 +-+-+-+-+-+-+-+-+
 |Z|N|H|C|0|0|0|0|
 +-+-+-+-+-+-+-+-+
 Z = Zero
 N = Subtract
 H = Half Carry
 C = Carry
 0 = Not used (always 0)
*/

#endif
