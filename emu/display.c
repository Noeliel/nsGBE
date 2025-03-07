// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

// ppu logic & display controller

// ppu stuff is implemented rather high level with little regard for clock cycles...
// ...instead, we just do all the work at once and idle on the remaining cycles
// also mode durations are hardcoded for now; on real hardware, there are slight...
// ...variations based on sprite count; here, we just have the longest possible hblank period.
// -> this may fail some tests, but it should be good enough for most games

#include "env.h"

#ifndef EMSCRIPTEN
#include <pthread.h>
pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
#endif

// todo: this most likely doesn't make much sense; redo
#define WINDOW_VISIBLE  ((int8_t)mem.raw[WY] >= 0 && \
                         (int8_t)mem.raw[WX] >= 0 && \
                         (uint8_t)mem.raw[WY] < 144 && \
                         (uint8_t)mem.raw[WX] < 167)

#define TILE_DATA_BLOCK_0_OFFSET 0x0
#define TILE_DATA_BLOCK_1_OFFSET 0x800
#define TILE_DATA_BLOCK_2_OFFSET 0x1000

#define TILE_DATA_BLOCK_0 0x8000 // todo: refactor dmg render code to work with vram map instead of mem.raw
#define TILE_DATA_BLOCK_1 0x8800 // ... and then discard all of these absolute offsets in favor of the relative ones (*_OFFSET)
#define TILE_DATA_BLOCK_2 0x9000

#define SPRITE_DATA_BLOCK_0_OFFSET 0x0
#define SPRITE_DATA_BLOCK_1_OFFSET 0x800

#define SPRITE_DATA_BLOCK_0 0x8000
#define SPRITE_DATA_BLOCK_1 0x8800

#define BG_WINDOW_TILE_MAP_1_OFFSET 0x1800
#define BG_WINDOW_TILE_MAP_2_OFFSET 0x1C00

#define BG_WINDOW_TILE_MAP_1 0x9800
#define BG_WINDOW_TILE_MAP_2 0x9C00

struct PPU_REGS ppu_regs;

_Bool ppu_alive = 0;

uint32_t ppu_clock_cycle_counter = 0;
int32_t ppu_exec_cycle_counter = 0;

uint32_t view_port_1[GB_FRAMEBUFFER_WIDTH * GB_FRAMEBUFFER_HEIGHT];
uint32_t view_port_2[GB_FRAMEBUFFER_WIDTH * GB_FRAMEBUFFER_HEIGHT];
uint32_t view_port_3[GB_FRAMEBUFFER_WIDTH * GB_FRAMEBUFFER_HEIGHT];

uint32_t *active_display_viewport = view_port_1;
uint32_t *next_display_viewport = view_port_2;
uint32_t *next_ppu_viewport = view_port_3;

_Bool new_frame_available = 0;

byte bg_color_indices[GB_FRAMEBUFFER_WIDTH * GB_FRAMEBUFFER_HEIGHT]; // which palette index a pixel had for transparency / blending

void (* display_notify_vblank)();

uint8_t window_internal_line_counter = 0;

/* DMG stuff */

byte dmg_color_palette[] = {0xFF, 0xAA, 0x55, 0x00};

union DMG_SPRITE_ATTRIBUTE_FLAGS {
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        byte unused         : 4; // CGB has different stuff here
        _Bool palette_num   : 1;
        _Bool x_flip        : 1;
        _Bool y_flip        : 1;
        _Bool bg_win_on_top : 1;
#else
        _Bool bg_win_on_top : 1;
        _Bool y_flip        : 1;
        _Bool x_flip        : 1;
        _Bool palette_num   : 1;
        byte unused         : 4; // CGB has different stuff here
#endif
    };
    byte b;
};

struct DMG_SPRITE_ATTRIBUTE {
#ifdef __LITTLE_ENDIAN__
    byte pos_y;
    byte pos_x;
    byte tile_index;
    union DMG_SPRITE_ATTRIBUTE_FLAGS flags;
#else
    union DMG_SPRITE_ATTRIBUTE_FLAGS flags;
    byte tile_index;
    byte pos_x;
    byte pos_y;
#endif
};

/* EOF DMG STUFF */

/* CGB stuff */

// pure RGB
#define RGB_RED(low, high) ((low & 0x1F) * 0x8)
#define RGB_GREEN(low, high) ((((low >> 5) | (((high & 0x3) << 3))) & 0x1F) * 0x8)
#define RGB_BLUE(low, high) (((high >> 2) & 0x1F) * 0x8)

// fast RGB -> CGB; method taken from https://gbdev.io/pandocs/#video-display
#define FCGB_RED(low, high) ((byte)(RGB_RED(low, high) * 0.75f + 0x8) & 0xFF)
#define FCGB_GREEN(low, high) ((byte)(RGB_GREEN(low, high) * 0.75f + 0x8) & 0xFF)
#define FCGB_BLUE(low, high) ((byte)(RGB_BLUE(low, high) * 0.75f + 0x8) & 0xFF)

// accurate RGB -> CGB
// values taken from
// https://github.com/libretro/glsl-shaders/blob/77697c58448380156a87b251e6461d5ab00071f3/handheld/shaders/color/gbc-color.glsl
// (source is public domain)
#define ACGB_r  0.82f
#define ACGB_gr 0.24f
#define ACGB_br -0.06f

#define ACGB_rg 0.125f
#define ACGB_g  0.665f
#define ACGB_bg 0.21f

#define ACGB_rb 0.195f
#define ACGB_b  0.73f
#define ACGB_gb 0.075f

#define ACGB_RED(low, high) (ACGB_r * RGB_RED(low, high) + ACGB_gr * RGB_GREEN(low, high) + ACGB_br * RGB_BLUE(low, high))
#define ACGB_GREEN(low, high) (ACGB_rg * RGB_RED(low, high) + ACGB_g * RGB_GREEN(low, high) + ACGB_bg * RGB_BLUE(low, high))
#define ACGB_BLUE(low, high) (ACGB_rb * RGB_RED(low, high) + ACGB_gb * RGB_GREEN(low, high) + ACGB_b * RGB_BLUE(low, high))

// clamped accurate RGB -> CGB (handles overflows and underflows)
#define CACGB_RED(low, high) ((tmp = ACGB_RED(low, high)) && tmp > 0xFF ? 0xFF : (tmp < 0 ? 0 : (byte)tmp))
#define CACGB_GREEN(low, high) ((tmp = ACGB_GREEN(low, high)) && tmp > 0xFF ? 0xFF : (tmp < 0 ? 0 : (byte)tmp))
#define CACGB_BLUE(low, high) ((tmp = ACGB_BLUE(low, high)) && tmp > 0xFF ? 0xFF : (tmp < 0 ? 0 : (byte)tmp))

int16_t tmp;

byte rgb_bg_color_palettes[0x40];
byte rgb_obj_color_palettes[0x40];

byte adjusted_bg_color_palettes_r[0x20];
byte adjusted_bg_color_palettes_g[0x20];
byte adjusted_bg_color_palettes_b[0x20];
byte adjusted_obj_color_palettes_r[0x20];
byte adjusted_obj_color_palettes_g[0x20];
byte adjusted_obj_color_palettes_b[0x20];

union CGB_SPRITE_ATTRIBUTE_FLAGS {
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        byte palette_num    : 3;
        _Bool vram_bank     : 1;
        _Bool unused        : 1;
        _Bool x_flip        : 1;
        _Bool y_flip        : 1;
        _Bool bg_win_on_top : 1;
#else
        _Bool bg_win_on_top : 1;
        _Bool y_flip        : 1;
        _Bool x_flip        : 1;
        _Bool unused        : 1;
        _Bool vram_bank     : 1;
        byte palette_num    : 3;
#endif
    };
    byte b;
};

struct CGB_SPRITE_ATTRIBUTE {
#ifdef __LITTLE_ENDIAN__
    byte pos_y;
    byte pos_x;
    byte tile_index;
    union CGB_SPRITE_ATTRIBUTE_FLAGS flags;
#else
    union CGB_SPRITE_ATTRIBUTE_FLAGS flags;
    byte tile_index;
    byte pos_x;
    byte pos_y;
#endif
};

union CGB_COLOR_PALETTE_SPECIFICATION {
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        byte index      : 6;
        byte unused     : 1;
        _Bool increment : 1;
#else
        _Bool increment : 1;
        byte unused     : 1;
        byte index      : 6;
#endif
    };
    word w;
} *color_palette_spec;

union CGB_BG_MAP_ATTRIBUTES {
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        byte bg_palette_num  : 3;
        _Bool vram_bank      : 1;
        _Bool unused         : 1;
        _Bool x_flip         : 1;
        _Bool y_flip         : 1;
        _Bool bg_to_oam_prio : 1;
#else
        _Bool bg_to_oam_prio : 1;
        _Bool y_flip         : 1;
        _Bool x_flip         : 1;
        _Bool unused         : 1;
        _Bool vram_bank      : 1;
        byte bg_palette_num  : 3;
#endif
    };
    word w;
} *bg_map_attributes;

/* EOF CGB stuff */

uint32_t *display_request_next_frame()
{
    if (new_frame_available)
    {
#ifndef EMSCRIPTEN
        pthread_mutex_lock(&mtx);
#endif

        uint32_t *tmp = next_display_viewport;
        next_display_viewport = active_display_viewport;
        active_display_viewport = tmp;

        new_frame_available = 0;

#ifndef EMSCRIPTEN
        pthread_mutex_unlock(&mtx);
#endif
    }

    return active_display_viewport;
}

void ppu_break()
{
    ppu_alive = 0;
}

__always_inline static void oam_search()
{

}

/* DMG rendering */

__always_inline static void draw_background_line_dmg(uint8_t line)
{
    byte color = 0x00;

    byte scx = mem.raw[SCX];
    byte scy = mem.raw[SCY];

    // addressing method
    uint16_t tile_data_base_block_0 = (ppu_regs.lcdc->bg_window_tile_data_area ? TILE_DATA_BLOCK_0 : TILE_DATA_BLOCK_2);
    uint16_t tile_data_base_block_1 = TILE_DATA_BLOCK_1;

    uint16_t bg_tile_map_base = (ppu_regs.lcdc->bg_tile_map_area ? BG_WINDOW_TILE_MAP_2 : BG_WINDOW_TILE_MAP_1);

    for (byte x = 0; x < GB_FRAMEBUFFER_WIDTH; x++)
    {
        // bg is enabled, render

        // which pixel in the bg map
        byte bg_map_pixel_index_x = (scx + x) % 256; // seems fine
        byte bg_map_pixel_index_y = (scy + line) % 256; // seems fine

        // which pixel in the tile
        byte bg_tile_pixel_index_x = bg_map_pixel_index_x % 8;
        byte bg_tile_pixel_index_y = bg_map_pixel_index_y % 8;

        // which tile
        byte bg_map_tile_index_x = bg_map_pixel_index_x / 8;
        byte bg_map_tile_index_y = bg_map_pixel_index_y / 8;

        //printf("bg_map_pixel_index_x: %d bg_map_pixel_index_y: %d\n", bg_map_pixel_index_x, bg_map_pixel_index_y);
        //printf("bg_map_tile_index_x: %d bg_map_tile_index_y: %d\n", bg_map_tile_index_x, bg_map_tile_index_y);

        // seems fine
        // bg tilemap is 32*32 tiles, layout is row by row
        byte bg_tile_index = mem.raw[bg_tile_map_base + bg_map_tile_index_x + (bg_map_tile_index_y * 32)];

        // printf("bg_tile_index: %d\n", bg_tile_index);

        uint16_t tile_offset = (bg_tile_index <= 127 ? tile_data_base_block_0 : tile_data_base_block_1);

        if (bg_tile_index > 127)
            bg_tile_index -= 128;

        // each tile is 16 bytes
        tile_offset += (16 * bg_tile_index);

        byte *tile_ptr = mem.raw + tile_offset;

        //printf("tile_offset: 0x%04X\n", tile_offset);

        // each tile has 2 bytes for each row
        byte *high = (tile_ptr + bg_tile_pixel_index_y * 2);
        byte *low = (tile_ptr + bg_tile_pixel_index_y * 2 + 1);

        //printf("high: 0x%02X low: 0x%02X\n", *high, *low);

        byte color_palette_index = 0;

        // horizontal pixel X in tile gets its color index from the Xth bit of the high and low bytes
        // ..where the low byte (little endian?) is shifted left by 1 (so 4 possible values)
        color_palette_index += ((*high >> (7 - bg_tile_pixel_index_x)) & 1);
        color_palette_index += ((*low >> (7 - bg_tile_pixel_index_x)) & 1) << 1;

        //printf("color_palette_index: %d\n", color_palette_index);

        byte color_index = mem.raw[BGP];

        /*
        Bit 7-6 - Shade for Color Number 3
        Bit 5-4 - Shade for Color Number 2
        Bit 3-2 - Shade for Color Number 1
        Bit 1-0 - Shade for Color Number 0
        */
        color_index = (color_index >> (color_palette_index * 2)) & 3;

        //printf("color_index: %d\n", color_index);

        color = dmg_color_palette[color_index];

        uint16_t pixel_index = x + (line % GB_FRAMEBUFFER_HEIGHT) * GB_FRAMEBUFFER_WIDTH;
        next_ppu_viewport[pixel_index] = (0xFF << 24) + (color << 16) + (color << 8) + color;
        bg_color_indices[pixel_index] = color_palette_index;
    }
}

__always_inline static void draw_window_line_dmg(uint8_t line)
{
    byte color = 0x00;

    byte wx = mem.raw[WX];
    byte wy = mem.raw[WY];

    int16_t real_window_origin_x = wx - 7;
    int16_t real_window_origin_y = wy;

    if (line < real_window_origin_y || real_window_origin_x < 0 || real_window_origin_x > 165 || real_window_origin_y > 143)
        return;

    // addressing method
    uint16_t tile_data_base_block_0 = (ppu_regs.lcdc->bg_window_tile_data_area ? TILE_DATA_BLOCK_0 : TILE_DATA_BLOCK_2);
    uint16_t tile_data_base_block_1 = TILE_DATA_BLOCK_1;

    uint16_t window_tile_map_base = (ppu_regs.lcdc->window_tile_map_area ? BG_WINDOW_TILE_MAP_2 : BG_WINDOW_TILE_MAP_1);

    for (byte x = real_window_origin_x; x < GB_FRAMEBUFFER_WIDTH; x++)
    {
        // window is enabled, render

        // which pixel in the window map
        byte window_map_pixel_index_x = x - real_window_origin_x; // seems fine
        byte window_map_pixel_index_y = line - real_window_origin_y; // seems fine

        //window_map_pixel_index_y += (window_internal_line_counter - window_map_pixel_index_y); // thanks, dmg-acid2

        // which pixel in the tile
        byte window_tile_pixel_index_x = window_map_pixel_index_x % 8;
        byte window_tile_pixel_index_y = window_map_pixel_index_y % 8;

        // which tile
        byte window_map_tile_index_x = window_map_pixel_index_x / 8;
        byte window_map_tile_index_y = window_map_pixel_index_y / 8;

        // seems fine
        // window tilemap is 32*32 tiles, layout is row by row
        byte window_tile_index = mem.raw[window_tile_map_base + window_map_tile_index_x + (window_map_tile_index_y * 32)];

        // printf("bg_tile_index: %d\n", bg_tile_index);

        uint16_t tile_offset = (window_tile_index <= 127 ? tile_data_base_block_0 : tile_data_base_block_1);

        if (window_tile_index > 127)
            window_tile_index -= 128;

        // each tile is 16 bytes
        tile_offset += (16 * window_tile_index);

        byte *tile_ptr = mem.raw + tile_offset;

        //printf("tile_offset: 0x%04X\n", tile_offset);

        // each tile has 2 bytes for each row
        byte *high = (tile_ptr + window_tile_pixel_index_y * 2);
        byte *low = (tile_ptr + window_tile_pixel_index_y * 2 + 1);

        //printf("high: 0x%02X low: 0x%02X\n", *high, *low);

        byte color_palette_index = 0;

        // horizontal pixel X in tile gets its color index from the Xth bit of the high and low bytes
        // ..where the low byte (little endian?) is shifted left by 1 (so 4 possible values)
        color_palette_index += ((*high >> (7 - window_tile_pixel_index_x)) & 1);
        color_palette_index += ((*low >> (7 - window_tile_pixel_index_x)) & 1) << 1;

        //printf("color_palette_index: %d\n", color_palette_index);

        byte color_index = mem.raw[BGP];

        /*
        Bit 7-6 - Shade for Color Number 3
        Bit 5-4 - Shade for Color Number 2
        Bit 3-2 - Shade for Color Number 1
        Bit 1-0 - Shade for Color Number 0
        */
        color_index = (color_index >> (color_palette_index * 2)) & 3;

        //printf("color_index: %d\n", color_index);

        color = dmg_color_palette[color_index];

        uint16_t pixel_index = x + (line % GB_FRAMEBUFFER_HEIGHT) * GB_FRAMEBUFFER_WIDTH;
        next_ppu_viewport[pixel_index] = (0xFF << 24) + (color << 16) + (color << 8) + color;
        bg_color_indices[pixel_index] = color_palette_index;
    }

    window_internal_line_counter++;
}

__always_inline static void draw_sprites_line_dmg(uint8_t line)
{
    byte color = 0x00;

    uint16_t sprite_data_base_block_0 = SPRITE_DATA_BLOCK_0;
    uint16_t sprite_data_base_block_1 = SPRITE_DATA_BLOCK_1;

    byte sprite_height = (ppu_regs.lcdc->obj_size ? 16 : 8);
    byte sprite_width = 8;

    for (byte oam_index = 40; oam_index > 0; oam_index--)
    {
        // if y is in sprite (handle 8x8 or 8x16)
            // write sprite pixel (x,y) to active_view_port

        struct DMG_SPRITE_ATTRIBUTE *spr_attrs = (struct DMG_SPRITE_ATTRIBUTE *)(mem.map.sprite_attr_table + ((oam_index - 1) * 4));

        int16_t real_sprite_origin_y = (spr_attrs->pos_y - 16);
        int16_t real_sprite_origin_x = (spr_attrs->pos_x - 8);

        if (line >= real_sprite_origin_y && line < (real_sprite_origin_y + sprite_height))
        {
            for (byte sprite_pixel_index_x = 0; sprite_pixel_index_x < 8; sprite_pixel_index_x++)
            {
                if (real_sprite_origin_x + sprite_pixel_index_x >= 0 && real_sprite_origin_x + sprite_pixel_index_x < GB_FRAMEBUFFER_WIDTH)
                {
                    byte sprite_pixel_index_y = line - real_sprite_origin_y;

                    byte sprite_tile_index = spr_attrs->tile_index;

                    if (ppu_regs.lcdc->obj_size)
                    {
                        // for y-flipping
                        if (spr_attrs->flags.y_flip)
                            sprite_pixel_index_y = 15 - sprite_pixel_index_y;

                        if (sprite_pixel_index_y < 8)
                            sprite_tile_index &= 0xFE;
                        else
                        {
                            sprite_pixel_index_y -= 8;
                            sprite_tile_index |= 0x01;
                        }
                    }
                    else
                    {
                        // for y-flipping
                        if (spr_attrs->flags.y_flip)
                            sprite_pixel_index_y = 7 - sprite_pixel_index_y;
                    }

                    uint16_t tile_offset = (sprite_tile_index <= 127 ? sprite_data_base_block_0 : sprite_data_base_block_1);

                    if (sprite_tile_index > 127)
                        sprite_tile_index -= 128;

                    tile_offset += ((2 * 8) * sprite_tile_index);

                    byte *tile_ptr = mem.raw + tile_offset;

                    byte *high = (tile_ptr + sprite_pixel_index_y * 2);
                    byte *low = (tile_ptr + sprite_pixel_index_y * 2 + 1);

                    byte color_palette_index = 0;

                    // for x-flipping
                    byte pixel_color_shift = (spr_attrs->flags.x_flip ? sprite_pixel_index_x : (7 - sprite_pixel_index_x));

                    color_palette_index += ((*high >> pixel_color_shift) & 1);
                    color_palette_index += ((*low >> pixel_color_shift) & 1) << 1;

                    byte color_index = mem.raw[(spr_attrs->flags.palette_num ? OBP1 : OBP0)];
                    color_index = (color_index >> (color_palette_index * 2)) & 3;

                    color = dmg_color_palette[color_index];

                    uint16_t pixel_index = real_sprite_origin_x + sprite_pixel_index_x + (mem.raw[LY] % GB_FRAMEBUFFER_HEIGHT) * GB_FRAMEBUFFER_WIDTH;

                    // this is not correct -- see pandocs note on sprite priorities and conflicts
                    if (color_palette_index != 0 && (!spr_attrs->flags.bg_win_on_top || bg_color_indices[pixel_index] == 0))
                        next_ppu_viewport[pixel_index] = (0xFF << 24) + (color << 16) + (color << 8) + color;
                }
            }
        }
    }
}

/* EOF DMG rendering */

/* CGB rendering */

__always_inline static void draw_background_line_cgb(uint8_t line)
{
    byte scx = mem.raw[SCX];
    byte scy = mem.raw[SCY];

    // addressing method
    // using the base instead as we're mapping directly onto vram, instead of mem.raw
    uint16_t tile_data_base_block_0 = (ppu_regs.lcdc->bg_window_tile_data_area ? TILE_DATA_BLOCK_0_OFFSET : TILE_DATA_BLOCK_2_OFFSET);
    uint16_t tile_data_base_block_1 = TILE_DATA_BLOCK_1_OFFSET;

    uint16_t bg_tile_map_base = (ppu_regs.lcdc->bg_tile_map_area ? BG_WINDOW_TILE_MAP_2_OFFSET : BG_WINDOW_TILE_MAP_1_OFFSET);

    for (byte x = 0; x < GB_FRAMEBUFFER_WIDTH; x++)
    {
        // bg is enabled, render

        // which pixel in the bg map
        byte bg_map_pixel_index_x = (scx + x) % 256; // seems fine
        byte bg_map_pixel_index_y = (scy + line) % 256; // seems fine

        // which pixel in the tile
        byte bg_tile_pixel_index_x = bg_map_pixel_index_x % 8;
        byte bg_tile_pixel_index_y = bg_map_pixel_index_y % 8;

        // which tile
        byte bg_map_tile_index_x = bg_map_pixel_index_x / 8;
        byte bg_map_tile_index_y = bg_map_pixel_index_y / 8;

        //printf("bg_map_pixel_index_x: %d bg_map_pixel_index_y: %d\n", bg_map_pixel_index_x, bg_map_pixel_index_y);
        //printf("bg_map_tile_index_x: %d bg_map_tile_index_y: %d\n", bg_map_tile_index_x, bg_map_tile_index_y);

        uint16_t bg_tile_map = bg_tile_map_base + bg_map_tile_index_x + (bg_map_tile_index_y * 32);

        bg_map_attributes = (union CGB_BG_MAP_ATTRIBUTES *)(cgb_extra_vram_bank + bg_tile_map);

        // seems fine
        // bg tilemap is 32*32 tiles, layout is row by row
        byte bg_tile_index = mem.map.video_ram[bg_tile_map];

        // printf("bg_tile_index: %d\n", bg_tile_index);

        uint16_t tile_offset = (bg_tile_index <= 127 ? tile_data_base_block_0 : tile_data_base_block_1);

        if (bg_tile_index > 127)
            bg_tile_index -= 128;

        // each tile is 16 bytes
        tile_offset += (16 * bg_tile_index);

        byte *tile_ptr = 0;

        if (bg_map_attributes->vram_bank == 1)
            tile_ptr = cgb_extra_vram_bank + tile_offset;
        else
            tile_ptr = mem.map.video_ram + tile_offset;

        //printf("tile_offset: 0x%04X\n", tile_offset);

        // for y-flipping
        if (bg_map_attributes->y_flip)
            bg_tile_pixel_index_y = 7 - bg_tile_pixel_index_y;

        // each tile has 2 bytes for each row
        byte *high = (tile_ptr + bg_tile_pixel_index_y * 2);
        byte *low = (tile_ptr + bg_tile_pixel_index_y * 2 + 1);

        //printf("high: 0x%02X low: 0x%02X\n", *high, *low);

        byte color_palette_index = 0;

        // for x-flipping
        byte pixel_color_shift = (bg_map_attributes->x_flip ? bg_tile_pixel_index_x : (7 - bg_tile_pixel_index_x));

        // horizontal pixel X in tile gets its color index from the Xth bit of the high and low bytes
        // ..where the low byte (little endian?) is shifted left by 1 (so 4 possible values)
        color_palette_index += ((*high >> pixel_color_shift) & 1);
        color_palette_index += ((*low >> pixel_color_shift) & 1) << 1;

        //printf("color_palette_index: %d\n", color_palette_index);

        // * 4 because 4 colors per palette, * 2 because 2 bytes per color
        //byte color_palette_low = cgb_bg_color_palettes[bg_map_attributes->bg_palette_num * 4 * 2 + (color_palette_index * 2)];
        //byte color_palette_high = cgb_bg_color_palettes[bg_map_attributes->bg_palette_num * 4 * 2 + (color_palette_index * 2) + 1];

        byte color_index = bg_map_attributes->bg_palette_num * 4 + color_palette_index;

        //printf("color_index: %d\n", color_index);

        uint16_t pixel_index = x + (line % GB_FRAMEBUFFER_HEIGHT) * GB_FRAMEBUFFER_WIDTH;
        next_ppu_viewport[pixel_index] = (0xFF << 24) + (adjusted_bg_color_palettes_b[color_index] << 16) \
          + (adjusted_bg_color_palettes_g[color_index] << 8) + adjusted_bg_color_palettes_r[color_index];

        if (ppu_regs.lcdc->bg_window_enable_prio)
        {
            if (bg_map_attributes->bg_to_oam_prio)
                bg_color_indices[pixel_index] = (color_palette_index == 0 ? 0 : 4);
            else
                bg_color_indices[pixel_index] = color_palette_index;
        }
        else
            bg_color_indices[pixel_index] = 5;
    }
}

__always_inline static void draw_window_line_cgb(uint8_t line)
{
    byte wx = mem.raw[WX];
    byte wy = mem.raw[WY];

    int16_t real_window_origin_x = wx - 7;
    int16_t real_window_origin_y = wy;

    if (line < real_window_origin_y || real_window_origin_x < 0 || real_window_origin_x > 165 || real_window_origin_y > 143)
        return;

    // addressing method
    uint16_t tile_data_base_block_0 = (ppu_regs.lcdc->bg_window_tile_data_area ? TILE_DATA_BLOCK_0_OFFSET : TILE_DATA_BLOCK_2_OFFSET);
    uint16_t tile_data_base_block_1 = TILE_DATA_BLOCK_1_OFFSET;

    uint16_t window_tile_map_base = (ppu_regs.lcdc->window_tile_map_area ? BG_WINDOW_TILE_MAP_2_OFFSET : BG_WINDOW_TILE_MAP_1_OFFSET);

    for (byte x = real_window_origin_x; x < GB_FRAMEBUFFER_WIDTH; x++)
    {
        // window is enabled, render

        // which pixel in the window map
        byte window_map_pixel_index_x = x - real_window_origin_x; // seems fine
        byte window_map_pixel_index_y = line - real_window_origin_y; // seems fine

        //window_map_pixel_index_y += (window_internal_line_counter - window_map_pixel_index_y); // thanks, cgb-acid2

        // which pixel in the tile
        byte window_tile_pixel_index_x = window_map_pixel_index_x % 8;
        byte window_tile_pixel_index_y = window_map_pixel_index_y % 8;

        // which tile
        byte window_map_tile_index_x = window_map_pixel_index_x / 8;
        byte window_map_tile_index_y = window_map_pixel_index_y / 8;

        uint16_t window_tile_map = window_tile_map_base + window_map_tile_index_x + (window_map_tile_index_y * 32);

        bg_map_attributes = (union CGB_BG_MAP_ATTRIBUTES *)(cgb_extra_vram_bank + window_tile_map);

        // seems fine
        // window tilemap is 32*32 tiles, layout is row by row
        byte window_tile_index = mem.map.video_ram[window_tile_map];

        // printf("bg_tile_index: %d\n", bg_tile_index);

        uint16_t tile_offset = (window_tile_index <= 127 ? tile_data_base_block_0 : tile_data_base_block_1);

        if (window_tile_index > 127)
            window_tile_index -= 128;

        // each tile is 16 bytes
        tile_offset += (16 * window_tile_index);

        byte *tile_ptr = 0;

        if (bg_map_attributes->vram_bank == 1)
            tile_ptr = cgb_extra_vram_bank + tile_offset;
        else
            tile_ptr = mem.map.video_ram + tile_offset;

        //printf("tile_offset: 0x%04X\n", tile_offset);

        // for y-flipping
        if (bg_map_attributes->y_flip)
            window_tile_pixel_index_y = 7 - window_tile_pixel_index_y;

        // each tile has 2 bytes for each row
        byte *high = (tile_ptr + window_tile_pixel_index_y * 2);
        byte *low = (tile_ptr + window_tile_pixel_index_y * 2 + 1);

        //printf("high: 0x%02X low: 0x%02X\n", *high, *low);

        byte color_palette_index = 0;

        // for x-flipping
        byte pixel_color_shift = (bg_map_attributes->x_flip ? window_tile_pixel_index_x : (7 - window_tile_pixel_index_x));

        // horizontal pixel X in tile gets its color index from the Xth bit of the high and low bytes
        // ..where the low byte (little endian?) is shifted left by 1 (so 4 possible values)
        color_palette_index += ((*high >> pixel_color_shift) & 1);
        color_palette_index += ((*low >> pixel_color_shift) & 1) << 1;

        //printf("color_palette_index: %d\n", color_palette_index);

        // * 4 because 4 colors per palette, * 2 because 2 bytes per color
        //byte color_palette_low = cgb_bg_color_palettes[bg_map_attributes->bg_palette_num * 4 * 2 + (color_palette_index * 2)];
        //byte color_palette_high = cgb_bg_color_palettes[bg_map_attributes->bg_palette_num * 4 * 2 + (color_palette_index * 2) + 1];

        byte color_index = bg_map_attributes->bg_palette_num * 4 + color_palette_index;

        //printf("color_index: %d\n", color_index);

        uint16_t pixel_index = x + (line % GB_FRAMEBUFFER_HEIGHT) * GB_FRAMEBUFFER_WIDTH;
        next_ppu_viewport[pixel_index] = (0xFF << 24) + (adjusted_bg_color_palettes_b[color_index] << 16) \
          + (adjusted_bg_color_palettes_g[color_index] << 8) + adjusted_bg_color_palettes_r[color_index];

        if (ppu_regs.lcdc->bg_window_enable_prio)
        {
            if (bg_map_attributes->bg_to_oam_prio)
                bg_color_indices[pixel_index] = (color_palette_index == 0 ? 0 : 4);
            else
                bg_color_indices[pixel_index] = color_palette_index;
        }
        else
            bg_color_indices[pixel_index] = 5;
    }

    window_internal_line_counter++;
}

__always_inline static void draw_sprites_line_cgb(uint8_t line)
{
    uint16_t sprite_data_base_block_0 = SPRITE_DATA_BLOCK_0_OFFSET;
    uint16_t sprite_data_base_block_1 = SPRITE_DATA_BLOCK_1_OFFSET;

    byte sprite_height = (ppu_regs.lcdc->obj_size ? 16 : 8);
    byte sprite_width = 8;

    for (byte oam_index = 40; oam_index > 0; oam_index--)
    {
        // if y is in sprite (handle 8x8 or 8x16)
            // write sprite pixel (x,y) to active_view_port

        struct CGB_SPRITE_ATTRIBUTE *spr_attrs = (struct CGB_SPRITE_ATTRIBUTE *)(mem.map.sprite_attr_table + ((oam_index - 1) * 4));

        int16_t real_sprite_origin_y = (spr_attrs->pos_y - 16);
        int16_t real_sprite_origin_x = (spr_attrs->pos_x - 8);

        if (line >= real_sprite_origin_y && line < (real_sprite_origin_y + sprite_height))
        {
            for (byte sprite_pixel_index_x = 0; sprite_pixel_index_x < 8; sprite_pixel_index_x++)
            {
                if (real_sprite_origin_x + sprite_pixel_index_x >= 0 && real_sprite_origin_x + sprite_pixel_index_x < GB_FRAMEBUFFER_WIDTH)
                {
                    byte sprite_pixel_index_y = line - real_sprite_origin_y;

                    byte sprite_tile_index = spr_attrs->tile_index;

                    if (ppu_regs.lcdc->obj_size)
                    {
                        // for y-flipping
                        if (spr_attrs->flags.y_flip)
                            sprite_pixel_index_y = 15 - sprite_pixel_index_y;

                        if (sprite_pixel_index_y < 8)
                            sprite_tile_index &= 0xFE;
                        else
                        {
                            sprite_pixel_index_y -= 8;
                            sprite_tile_index |= 0x01;
                        }
                    }
                    else
                    {
                        // for y-flipping
                        if (spr_attrs->flags.y_flip)
                            sprite_pixel_index_y = 7 - sprite_pixel_index_y;
                    }

                    uint16_t tile_offset = (sprite_tile_index <= 127 ? sprite_data_base_block_0 : sprite_data_base_block_1);

                    if (sprite_tile_index > 127)
                        sprite_tile_index -= 128;

                    tile_offset += ((2 * 8) * sprite_tile_index);

                    byte *tile_ptr = 0;

                    if (spr_attrs->flags.vram_bank == 1)
                        tile_ptr = cgb_extra_vram_bank + tile_offset;
                    else
                        tile_ptr = mem.map.video_ram + tile_offset;

                    byte *high = (tile_ptr + sprite_pixel_index_y * 2);
                    byte *low = (tile_ptr + sprite_pixel_index_y * 2 + 1);

                    byte color_palette_index = 0;

                    // for x-flipping
                    byte pixel_color_shift = (spr_attrs->flags.x_flip ? sprite_pixel_index_x : (7 - sprite_pixel_index_x));

                    color_palette_index += ((*high >> pixel_color_shift) & 1);
                    color_palette_index += ((*low >> pixel_color_shift) & 1) << 1;

                    //byte color_palette_low = cgb_obj_color_palettes[spr_attrs->flags.palette_num * 4 * 2 + (color_palette_index * 2)];
                    //byte color_palette_high = cgb_obj_color_palettes[spr_attrs->flags.palette_num * 4 * 2 + (color_palette_index * 2) + 1];

                    byte color_index = spr_attrs->flags.palette_num * 4 + color_palette_index;

                    uint16_t pixel_index = real_sprite_origin_x + sprite_pixel_index_x + (mem.raw[LY] % GB_FRAMEBUFFER_HEIGHT) * GB_FRAMEBUFFER_WIDTH;

                    // this is not correct -- see pandocs note on sprite priorities and conflicts
                    if (color_palette_index != 0 && (bg_color_indices[pixel_index] == 5 || (bg_color_indices[pixel_index] != 4 \
                      && (!spr_attrs->flags.bg_win_on_top || bg_color_indices[pixel_index] == 0))))
                        next_ppu_viewport[pixel_index] = (0xFF << 24) + (adjusted_obj_color_palettes_b[color_index] << 16) \
                          + (adjusted_obj_color_palettes_g[color_index] << 8) + adjusted_obj_color_palettes_r[color_index];
                }
            }
        }
    }
}

/* EOF CGB rendering */

__always_inline static void render_scanline()
{
    byte line = mem.raw[LY];

    if (gb_mode == MODE_CGB)
    {
        draw_background_line_cgb(line);

        if (ppu_regs.lcdc->window_enable)
            draw_window_line_cgb(line);

        if (ppu_regs.lcdc->obj_enable)
            draw_sprites_line_cgb(line);
    }
    else
    {
        if (ppu_regs.lcdc->bg_window_enable_prio)
        {
            draw_background_line_dmg(line);

            if (ppu_regs.lcdc->window_enable && WINDOW_VISIBLE)
                draw_window_line_dmg(line);
        }
        else
            for (byte x = 0; x < GB_FRAMEBUFFER_WIDTH; x++)
            {
                uint16_t pixel_index = x + (mem.raw[LY] % GB_FRAMEBUFFER_HEIGHT) * GB_FRAMEBUFFER_WIDTH;
                next_ppu_viewport[pixel_index] = 0xFFFFFFFF;
                bg_color_indices[pixel_index] = 0;
            }

        if (ppu_regs.lcdc->obj_enable)
            draw_sprites_line_dmg(line);
    }
}

__always_inline static void oam_read()
{
    // do oam read stuff
}

__always_inline static void vram_read()
{
    // do vram read stuff

    // render scanline
    render_scanline();
}

__always_inline static void hblank()
{
    // do hblank stuff
    // todo: maybe this has to be mem.raw[LY] + 1 instead
    ppu_regs.stat->lyc_eq_ly = (mem.raw[LYC] == mem.raw[LY] + 1);

    if ((ppu_regs.stat->lyc_eq_ly || ppu_regs.stat->hblank_int) && mem.map.interrupt_enable_reg.LCD_STAT)
        mem.map.interrupt_flag_reg.LCD_STAT = 1;
}

__always_inline static void vblank()
{
    window_internal_line_counter = 0;

    // do vblank stuff

#ifndef EMSCRIPTEN
    pthread_mutex_lock(&mtx);
#endif

    uint32_t *tmp = next_ppu_viewport;
    next_ppu_viewport = next_display_viewport;
    next_display_viewport = tmp;

    new_frame_available = 1;

#ifndef EMSCRIPTEN
    pthread_mutex_unlock(&mtx);
#endif

    //printf("drawing frame\n");
    if (display_notify_vblank)
        display_notify_vblank();

    mem.map.interrupt_flag_reg.VBLANK = 1;
}

__always_inline void ppu_step()
{
    ppu_clock_cycle_counter++;

    if (mem.raw[LY] <= 143)
    {
        // todo: improve this so it doesn't cycle through all the modes all the time
        if (ppu_clock_cycle_counter > 456) // leave HBLANK_MODE; hardcoded duration 204, it's 85 to 208 depending on previous duration
        {
            ppu_clock_cycle_counter = 0;
            mem.raw[LY]++;
            //DEBUG_PRINT(("drawing scanline 0x%02X\n", mem.raw[LY]));
            ppu_regs.stat->mode = PPU_OAM_READ_MODE;
            oam_read();
        }
        else if (ppu_clock_cycle_counter == 253) // leave VRAM_READ_MODE; hardcoded duration 172, it's 168 to 291 depending on sprite count
        {
            ppu_regs.stat->mode = PPU_HBLANK_MODE;
            hblank();
        }
        else if (ppu_clock_cycle_counter == 81) // leave OAM_READ_MODE
        {
            ppu_regs.stat->mode = PPU_VRAM_READ_MODE;
            vram_read();
        }
    }
    else if (mem.raw[LY] >= 144)
    {
        ppu_regs.stat->mode = PPU_VBLANK_MODE;

        mem.raw[LY] = 144 + (ppu_clock_cycle_counter / 456);

        if (ppu_clock_cycle_counter > 4560)
        {
            vblank();
            ppu_clock_cycle_counter = 0;
            mem.raw[LY] = 0;
            ppu_regs.stat->mode = PPU_OAM_READ_MODE;
            oam_read();
        }
    }
}

__always_inline int32_t ppu_exec_cycles(int32_t clock_cycles_to_execute)
{
    ppu_exec_cycle_counter = 0;

    if (ppu_regs.lcdc->lcd_ppu_enable)
    {
        while (ppu_exec_cycle_counter < clock_cycles_to_execute)
        {
            ppu_step();
            ppu_exec_cycle_counter++;
        }
    }

    return 0;
}

void hi_test()
{
    // H

    next_ppu_viewport[4 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;
    next_ppu_viewport[5 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;
    next_ppu_viewport[6 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;
    next_ppu_viewport[7 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;
    next_ppu_viewport[8 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;

    next_ppu_viewport[4 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;
    next_ppu_viewport[5 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;
    next_ppu_viewport[6 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;
    next_ppu_viewport[7 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;
    next_ppu_viewport[8 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;

    next_ppu_viewport[6 * GB_FRAMEBUFFER_WIDTH + 6] = 0xFF;
    next_ppu_viewport[6 * GB_FRAMEBUFFER_WIDTH + 7] = 0xFF;

    next_ppu_viewport[4 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;
    next_ppu_viewport[5 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;
    next_ppu_viewport[6 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;
    next_ppu_viewport[7 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;
    next_ppu_viewport[8 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;

    next_ppu_viewport[4 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;
    next_ppu_viewport[5 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;
    next_ppu_viewport[6 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;
    next_ppu_viewport[7 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;
    next_ppu_viewport[8 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;

    // I

    next_ppu_viewport[4 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;
    next_ppu_viewport[5 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;
    next_ppu_viewport[6 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;
    next_ppu_viewport[7 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;
    next_ppu_viewport[8 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;

    next_ppu_viewport[4 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;
    next_ppu_viewport[5 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;
    next_ppu_viewport[6 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;
    next_ppu_viewport[7 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;
    next_ppu_viewport[8 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;

    // !

    next_ppu_viewport[4 * GB_FRAMEBUFFER_WIDTH + 17] = 0xFF;
    next_ppu_viewport[5 * GB_FRAMEBUFFER_WIDTH + 17] = 0xFF;
    next_ppu_viewport[8 * GB_FRAMEBUFFER_WIDTH + 17] = 0xFF;

    next_ppu_viewport[4 * GB_FRAMEBUFFER_WIDTH + 18] = 0xFF;
    next_ppu_viewport[5 * GB_FRAMEBUFFER_WIDTH + 18] = 0xFF;
    next_ppu_viewport[6 * GB_FRAMEBUFFER_WIDTH + 18] = 0xFF;
    next_ppu_viewport[8 * GB_FRAMEBUFFER_WIDTH + 18] = 0xFF;
}

__always_inline void adjust_bg_color_palettes(byte index, byte low, byte high)
{
#if EMULATED_CGB_DISPLAY_TONE == 2
    adjusted_bg_color_palettes_r[index] = CACGB_RED(low, high);
    adjusted_bg_color_palettes_g[index] = CACGB_GREEN(low, high);
    adjusted_bg_color_palettes_b[index] = CACGB_BLUE(low, high);
#elif EMULATED_CGB_DISPLAY_TONE == 1
    adjusted_bg_color_palettes_r[index] = FCGB_RED(low, high);
    adjusted_bg_color_palettes_g[index] = FCGB_GREEN(low, high);
    adjusted_bg_color_palettes_b[index] = FCGB_BLUE(low, high);
#else
    adjusted_bg_color_palettes_r[index] = RGB_RED(low, high);
    adjusted_bg_color_palettes_g[index] = RGB_GREEN(low, high);
    adjusted_bg_color_palettes_b[index] = RGB_BLUE(low, high);
#endif
}

__always_inline void adjust_obj_color_palettes(byte index, byte low, byte high)
{
#if EMULATED_CGB_DISPLAY_TONE == 2
    adjusted_obj_color_palettes_r[index] = CACGB_RED(low, high);
    adjusted_obj_color_palettes_g[index] = CACGB_GREEN(low, high);
    adjusted_obj_color_palettes_b[index] = CACGB_BLUE(low, high);
#elif EMULATED_CGB_DISPLAY_TONE == 1
    adjusted_obj_color_palettes_r[index] = FCGB_RED(low, high);
    adjusted_obj_color_palettes_g[index] = FCGB_GREEN(low, high);
    adjusted_obj_color_palettes_b[index] = FCGB_BLUE(low, high);
#else
    adjusted_obj_color_palettes_r[index] = RGB_RED(low, high);
    adjusted_obj_color_palettes_g[index] = RGB_GREEN(low, high);
    adjusted_obj_color_palettes_b[index] = RGB_BLUE(low, high);
#endif
}

void ppu_reset()
{
    ppu_alive = 1;

    // set up registers by pointing them to the correct locations in memory
    ppu_regs.lcdc = (union PPU_LCDC *)(mem.raw + LCDC);
    ppu_regs.stat = (union PPU_STAT *)(mem.raw + STAT);

    ppu_regs.stat->mode = PPU_OAM_READ_MODE;

    // hi_test();
}

__always_inline uint16_t ppu_interpret_read(uint16_t offset)
{
    if (gb_mode == MODE_CGB)
    {
        if (offset == BCPD)
        {
            color_palette_spec = (union CGB_COLOR_PALETTE_SPECIFICATION *)(mem.raw + BCPS);
            return (0x100 | rgb_bg_color_palettes[color_palette_spec->index]);
        }

        if (offset == OCPD)
        {
            color_palette_spec = (union CGB_COLOR_PALETTE_SPECIFICATION *)(mem.raw + OCPS);
            return (0x100 | rgb_obj_color_palettes[color_palette_spec->index]);
        }
    }

    return 0;
}

__always_inline uint16_t ppu_interpret_write(uint16_t offset, byte data)
{
    if (offset == LY)
        return 0x100; // LY is read-only

    if (offset >= OAM && offset <= OAM_END) // we can only write here during hblank and vblank
        if (ppu_regs.stat->mode == PPU_OAM_READ_MODE || ppu_regs.stat->mode == PPU_VRAM_READ_MODE)
        {
            DEBUG_PRINT(("ppu blocking mistimed oam write\n"));
            return 0x100;
        }

    if (gb_mode != MODE_CGB)
    {
        if (offset >= BCPD && offset <= OCPD) // we can't write here during vram read mode (3)
            if (ppu_regs.stat->mode == PPU_VRAM_READ_MODE)
            {
                DEBUG_PRINT(("ppu blocking mistimed palette data write\n"));
                return 0x100;
            }
    }

    if (gb_mode == MODE_CGB)
    {
        if (offset == BCPD)
        {
            color_palette_spec = (union CGB_COLOR_PALETTE_SPECIFICATION *)(mem.raw + BCPS);

            rgb_bg_color_palettes[color_palette_spec->index] = data;

            byte low_index = color_palette_spec->index & 0xFE;
            byte high_index = low_index + 1;

            adjust_bg_color_palettes((low_index / 2), rgb_bg_color_palettes[low_index], rgb_bg_color_palettes[high_index]);

            if (color_palette_spec->increment)
                color_palette_spec->index++;

            return 0x100;
        }

        if (offset == OCPD)
        {
            color_palette_spec = (union CGB_COLOR_PALETTE_SPECIFICATION *)(mem.raw + OCPS);

            rgb_obj_color_palettes[color_palette_spec->index] = data;

            byte low_index = color_palette_spec->index & 0xFE;
            byte high_index = low_index + 1;

            adjust_obj_color_palettes((low_index / 2), rgb_obj_color_palettes[low_index], rgb_obj_color_palettes[high_index]);

            if (color_palette_spec->increment)
                color_palette_spec->index++;

            return 0x100;
        }
    }

    return 0;
}
