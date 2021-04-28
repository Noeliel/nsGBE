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


// ppu logic & display controller

// ppu stuff is implemented rather high level with little regard for clock cycles...
// ...instead, we just do all the work at once and idle on the remaining cycles
// also mode durations are hardcoded for now; on real hardware, there are slight...
// ...variations based on sprite count; here, we just have the longest possible hblank period.
// -> this may fail some tests, but it should be good enough for emulation

#include <env.h>

#define WINDOW_VISIBLE ((int8_t)mem.raw[WY] >= 0 && (int8_t)mem.raw[WX] >= 7)

byte dmg_color_palette[] = {0xFF, 0xAA, 0x55, 0x00};

#define TILE_DATA_BLOCK_0 0x8000
#define TILE_DATA_BLOCK_1 0x8800
#define TILE_DATA_BLOCK_2 0x9000

#define SPRITE_DATA_BLOCK_0 0x8000
#define SPRITE_DATA_BLOCK_1 0x8800

#define BG_WINDOW_TILE_MAP_1 0x9800
#define BG_WINDOW_TILE_MAP_2 0x9C00

union SPRITE_ATTRIBUTE_FLAGS_DMG {
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

struct SPRITE_ATTRIBUTE_DMG {
#ifdef __LITTLE_ENDIAN__
    byte pos_y;
    byte pos_x;
    byte tile_index;
    union SPRITE_ATTRIBUTE_FLAGS_DMG flags;
#else
    union SPRITE_ATTRIBUTE_FLAGS_DMG flags;
    byte tile_index;
    byte pos_x;
    byte pos_y;
#endif
};

struct PPU_REGS ppu_regs;

_Bool ppu_alive = 0;

uint32_t ppu_clock_cycle_counter = 0;
int32_t ppu_exec_cycle_counter = 0;

byte view_port_1[GB_FRAMEBUFFER_WIDTH * GB_FRAMEBUFFER_HEIGHT];
byte view_port_2[GB_FRAMEBUFFER_WIDTH * GB_FRAMEBUFFER_HEIGHT];
byte view_port_3[GB_FRAMEBUFFER_WIDTH * GB_FRAMEBUFFER_HEIGHT];

byte *active_display_viewport = view_port_1;
byte *next_display_viewport = view_port_2;
byte *next_ppu_viewport = view_port_3;

_Bool swapping_buffers = 0;
_Bool new_frame_available = 0;

byte bg_color_indices[GB_FRAMEBUFFER_WIDTH * GB_FRAMEBUFFER_HEIGHT];

void (* display_notify_vblank)();

_Bool did_oam_read = 0;
_Bool did_vram_read = 0;
_Bool did_hblank = 0;
_Bool did_vblank = 0;

uint8_t *display_request_next_frame()
{
    if (new_frame_available)
    {
        while (swapping_buffers)
        { printf(""); }

        swapping_buffers = 1;

        byte *tmp = next_display_viewport;
        next_display_viewport = active_display_viewport;
        active_display_viewport = tmp;

        new_frame_available = 0;

        swapping_buffers = 0;
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

__always_inline static void draw_background_line(uint8_t line)
{
    byte color = 0x00;

    byte scx = mem.raw[SCX];
    byte scy = mem.raw[SCY];

    // addressing method
    uint16_t tile_data_base_block_0 = (ppu_regs.lcdc->bg_window_tile_data_area ? TILE_DATA_BLOCK_0 : TILE_DATA_BLOCK_2);
    uint16_t tile_data_base_block_1 = TILE_DATA_BLOCK_1;

    uint16_t bg_tile_map = (ppu_regs.lcdc->bg_tile_map_area ? BG_WINDOW_TILE_MAP_2 : BG_WINDOW_TILE_MAP_1);

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
        byte bg_tile_index = mem.raw[bg_tile_map + bg_map_tile_index_x + (bg_map_tile_index_y * 32)];

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
        next_ppu_viewport[pixel_index] = color;
        bg_color_indices[pixel_index] = color_palette_index;
    }
}

__always_inline static void draw_window_line(uint8_t line)
{
    byte color = 0x00;

    byte wx = mem.raw[WX];
    byte wy = mem.raw[WY];

    int16_t real_window_origin_x = wx - 7;
    int16_t real_window_origin_y = wy;

    if (line < real_window_origin_y)
        return;

    // addressing method
    uint16_t tile_data_base_block_0 = (ppu_regs.lcdc->bg_window_tile_data_area ? TILE_DATA_BLOCK_0 : TILE_DATA_BLOCK_2);
    uint16_t tile_data_base_block_1 = TILE_DATA_BLOCK_1;

    uint16_t window_tile_map = (ppu_regs.lcdc->window_tile_map_area ? BG_WINDOW_TILE_MAP_2 : BG_WINDOW_TILE_MAP_1);

    for (byte x = real_window_origin_x; x < GB_FRAMEBUFFER_WIDTH; x++)
    {
        // window is enabled, render

        // which pixel in the window map
        byte window_map_pixel_index_x = x - real_window_origin_x; // seems fine
        byte window_map_pixel_index_y = line - real_window_origin_y; // seems fine

        // which pixel in the tile
        byte window_tile_pixel_index_x = window_map_pixel_index_x % 8;
        byte window_tile_pixel_index_y = window_map_pixel_index_y % 8;

        // which tile
        byte window_map_tile_index_x = window_map_pixel_index_x / 8;
        byte window_map_tile_index_y = window_map_pixel_index_y / 8;

        // seems fine
        // window tilemap is 32*32 tiles, layout is row by row
        byte window_tile_index = mem.raw[window_tile_map + window_map_tile_index_x + (window_map_tile_index_y * 32)];

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
        next_ppu_viewport[pixel_index] = color;
        bg_color_indices[pixel_index] = color_palette_index;
    }
}

__always_inline static void draw_sprites_on_line(uint8_t line)
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

        struct SPRITE_ATTRIBUTE_DMG *spr_attrs = mem.map.sprite_attr_table + ((oam_index - 1) * 4);

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

                    uint16_t tile_offset = (sprite_tile_index <= 127 ? sprite_data_base_block_0 : sprite_data_base_block_1);

                    if (sprite_tile_index > 127)
                        sprite_tile_index -= 128;

                    tile_offset += ((2 * 8) * sprite_tile_index);

                    byte *tile_ptr = mem.raw + tile_offset;

                    // for y-flipping
                    if (spr_attrs->flags.y_flip)
                        sprite_pixel_index_y = 7 - sprite_pixel_index_y;                      

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
                        next_ppu_viewport[pixel_index] = color;
                }
            }
        }
    }
}

__always_inline static void render_scanline()
{
    byte line = mem.raw[LY];

    if (ppu_regs.lcdc->bg_window_enable_prio)
    {
        draw_background_line(line);
    
        if (ppu_regs.lcdc->window_enable && WINDOW_VISIBLE)
            draw_window_line(line);
    }
    else
        for (byte x = 0; x < GB_FRAMEBUFFER_WIDTH; x++)
        {
            uint16_t pixel_index = x + (mem.raw[LY] % GB_FRAMEBUFFER_HEIGHT) * GB_FRAMEBUFFER_WIDTH;
            next_ppu_viewport[pixel_index] = 0xFF;
            bg_color_indices[pixel_index] = 0;
        }

    if (ppu_regs.lcdc->obj_enable)
        draw_sprites_on_line(line);
}

__always_inline static void oam_read()
{
    if (!did_oam_read)
    {
        did_oam_read = 1;

        // do oam read stuff
    }
    
    ppu_clock_cycle_counter++;
}

__always_inline static void vram_read()
{
    if (!did_vram_read)
    {
        did_vram_read = 1;

        // do vram read stuff

        // render scanline
        render_scanline();
    }

    ppu_clock_cycle_counter++;
}

__always_inline static void hblank()
{
    if (!did_hblank)
    {
        did_hblank = 1;

        // do hblank stuff
        // todo: maybe this has to be mem.raw[LY] + 1 instead
        ppu_regs.stat->lyc_eq_ly = (mem.raw[LYC] == mem.raw[LY]);

        if ((ppu_regs.stat->lyc_eq_ly || ppu_regs.stat->hblank_int) && mem.map.interrupt_enable_reg.LCD_STAT)
            mem.map.interrupt_flag_reg.LCD_STAT = 1;
    }
    
    ppu_clock_cycle_counter++;
}

__always_inline static void vblank()
{
    if (!did_vblank)
    {
        did_vblank = 1;

        // do vblank stuff

        while (swapping_buffers)
        { usleep(1); }

        swapping_buffers = 1;

        uint8_t *tmp = next_ppu_viewport;
        next_ppu_viewport = next_display_viewport;
        next_display_viewport = tmp;

        new_frame_available = 1;

        swapping_buffers = 0;

        //printf("drawing frame\n");
        if (display_notify_vblank)
            display_notify_vblank();
        
        if (mem.map.interrupt_enable_reg.VBLANK)
            mem.map.interrupt_flag_reg.VBLANK = 1;
    }
    
    ppu_clock_cycle_counter++;
}

__always_inline void ppu_step()
{
    switch(ppu_regs.stat->mode)
    {
        case PPU_OAM_READ_MODE:
            did_vram_read = 0;
            did_hblank = 0;
            did_vblank = 0;
            oam_read();
            break;
        
        case PPU_VRAM_READ_MODE:
            did_oam_read = 0;
            did_hblank = 0;
            did_vblank = 0;
            vram_read();
            break;
        
        case PPU_HBLANK_MODE:
            did_oam_read = 0;
            did_vram_read = 0;
            did_vblank = 0;
            hblank();
            break;
        
        case PPU_VBLANK_MODE:
            did_oam_read = 0;
            did_vram_read = 0;
            did_hblank = 0;
            vblank();
            break;
        
        default:
            break;
    }

    if (mem.raw[LY] <= 143)
    {
        // todo: improve this so it doesn't cycle through all the modes all the time
        if (ppu_clock_cycle_counter >= 80)
            ppu_regs.stat->mode = PPU_VRAM_READ_MODE;
        if (ppu_clock_cycle_counter >= 252) // hardcoded duration 172, it's 168 to 291 depending on sprite count
            ppu_regs.stat->mode = PPU_HBLANK_MODE;
        if (ppu_clock_cycle_counter >= 456) // hardcoded duration 204, it's 85 to 208 depending on previous duration
        {
            ppu_clock_cycle_counter -= 456;
            mem.raw[LY]++;
            //DEBUG_PRINT(("drawing scanline 0x%02X\n", mem.raw[LY]));
            ppu_regs.stat->mode = PPU_OAM_READ_MODE;
        }
    }
    else if (mem.raw[LY] >= 144)
    {
        ppu_regs.stat->mode = PPU_VBLANK_MODE;

        mem.raw[LY] = 144 + (ppu_clock_cycle_counter / 456);

        if (ppu_clock_cycle_counter >= 4560)
        {
            ppu_clock_cycle_counter -= 4560;
            mem.raw[LY] = 0;
            ppu_regs.stat->mode = PPU_OAM_READ_MODE;
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

void ppu_reset()
{
    ppu_alive = 1;

    // set up registers by pointing them to the correct locations in memory
    ppu_regs.lcdc = mem.raw + LCDC;
    ppu_regs.stat = mem.raw + STAT;

    ppu_regs.stat->mode = PPU_OAM_READ_MODE;

    // hi_test();
}

__always_inline uint16_t ppu_interpret_read(uint16_t offset)
{
    return 0;
}

__always_inline uint16_t ppu_interpret_write(uint16_t offset, byte data)
{
    if (offset >= OAM && offset <= OAM_END) // we can only write here during hblank and vblank
        if (ppu_regs.stat->mode == PPU_OAM_READ_MODE || ppu_regs.stat->mode == PPU_VRAM_READ_MODE)
            return 0x100;
    
     if (offset >= BGPD && offset <= OBPD) // we can't write here during vram read mode (3)
        if (ppu_regs.stat->mode == PPU_VRAM_READ_MODE)
            return 0x100;

    switch (offset)
    {        
        case LY: // LY is read-only
            return 0x100;
        
        default:
            break;
    }
    
    return 0;
}
