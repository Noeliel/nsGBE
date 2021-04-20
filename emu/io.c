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


#include "env.h"

#define IO_BOOTROM_CONTROL 0xFF50
#define IO_JOYPAD          0xFF00
#define IO_DIVIDER         0xFF04
#define IO_TIMER           0xFF05

uint32_t io_exec_cycle_counter = 0;

byte dma_byte;
uint16_t dma_timer = 0;

uint32_t divider_counter;

union BUTTON_STATE unencoded_button_state; // local copy of button_states
extern union BUTTON_STATE button_states; // external state (in platform-specific gui implementation)

__always_inline byte dma_transfer()
{
    if (dma_timer % 4 == 0)
    {
        word source = word(0x0000);
        source.b.h = dma_byte;
    
        byte i = 0xA0 - (dma_timer / 4);
        mem.map.sprite_attr_table[i] = mem_read(source.w + i);
    }

    dma_timer--;
}

__always_inline static void encode_joypad_byte(byte data)
{
    union JOYPAD_IO *jp = &data;

    _Bool as_action = (jp->ACT ? 0 : 1);
    _Bool as_direction = (jp->DIR ? 0 : 1);

    jp = mem.raw + IO_JOYPAD;

    // JOYPAD_IO bits are inverted (0 to select / press)

    if (as_action)
    {
        jp->RA = !unencoded_button_state.A;
        jp->LB = !unencoded_button_state.B;
        jp->USEL = !unencoded_button_state.SELECT;
        jp->DSTR = !unencoded_button_state.START;
    }
    else if (as_direction)
    {
        jp->RA = !unencoded_button_state.RIGHT;
        jp->LB = !unencoded_button_state.LEFT;
        jp->USEL = !unencoded_button_state.UP;
        jp->DSTR = !unencoded_button_state.DOWN;
    }
    else
        jp->b = 0xFF;
    
    jp->unused = 3;
}

__always_inline static void sync_button_states()
{    
    if (unencoded_button_state.b != button_states.b)
        if (mem.map.interrupt_enable_reg.JOYPAD)
            mem.map.interrupt_flag_reg.JOYPAD = 1;

    unencoded_button_state.b = button_states.b;

    //encode_joypad_byte(0);
}

__always_inline uint16_t io_interpret_read(uint16_t offset)
{
    //if (offset == IO_JOYPAD) // keypad register; todo: better
        //return 0x1FF;

    if (offset >= OAM && offset <= OAM_END && dma_timer > 0)
        return 0x1FF;
    
    return 0;
}

__always_inline uint16_t io_interpret_write(uint16_t offset, byte data)
{
    if (offset == IO_BOOTROM_CONTROL)
        if (data > 0)
            enable_bootrom = 0;
    
    if (offset == DMA)
    {
        if (dma_timer == 0)
        {
            dma_byte = (data > 0xDF ? 0xDF : data);
            dma_timer = 160 * 4;
        }
        else
            return 0x100;
    }
    
    if (offset == IO_JOYPAD)
    {
        encode_joypad_byte(data);
        return 0x100;
    }

    if (offset == IO_DIVIDER)
    {
        mem.raw[IO_DIVIDER] = 0;
        return 0x100;
    }
    
    if (offset == IO_TIMER)
    {
        mem.raw[IO_TIMER] = 0;
        return 0x100;
    }

    return 0;
}

__always_inline void io_step()
{
    if (dma_timer > 0)
        dma_transfer();
    
    divider_counter++;

    if (divider_counter > 256)
    {
        mem.raw[IO_DIVIDER] += 1;
        divider_counter = 0;
    }

    //if (mem.raw[IO_TIMER] < old)
        //mem.map.interrupt_flag_reg.TIMER = 1;

    sync_button_states();
}

__always_inline int32_t io_exec_cycles(uint32_t clock_cycles_to_execute)
{
    io_exec_cycle_counter = 0;

    while (io_exec_cycle_counter < clock_cycles_to_execute)
    {
            io_step();
            io_exec_cycle_counter++;
    }

    return 0;
}

