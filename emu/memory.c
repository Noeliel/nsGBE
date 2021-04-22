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


// memory controller

#include "env.h"

union MEMORY mem;

void *map_to_physical_location(uint16_t offset);
uint16_t redirect_ram_echo(uint16_t offset);
void *redirect_to_active_rom_bank(uint16_t offset);
void *redirect_to_active_cart_ram_bank(uint16_t offset);

_Bool enable_bootrom = 0;

__always_inline byte mem_read(uint16_t offset)
{
    // < 0x100: continue; 0x1XX: return XX
    uint16_t component_response = 0;
    
    offset &= 0xFFFF;

    component_response = io_interpret_read(offset);
    if (component_response > 0xFF)
        return (component_response & 0xFF);

    if (enable_bootrom)
        if (offset <= 0xFF)
            return *((byte *)biosbuffer + offset);

    component_response = ppu_interpret_read(offset);
    if (component_response > 0xFF)
        return (component_response & 0xFF);

    component_response = mbc_interpret_read(offset);
    if (component_response > 0xFF)
        return (component_response & 0xFF);
        
    return (* (byte *)map_to_physical_location(offset));
}

__always_inline word mem_read_16(uint16_t offset) // simulating little endian byte order
{
    if (offset > 0xFFFE)
        offset = 0xFFFE;

    byte low = mem_read(offset);
    byte high = mem_read(offset + 1);
    
    word data;
    data.b.l = low;
    data.b.h = high;
    return data;
}

__always_inline void mem_write(uint16_t offset, byte data)
{
    offset &= 0xFFFF;
    data &= 0xFF;

    if (io_interpret_write(offset, data) == 0x100) // < 0x100: continue; == 0x100: block, return
        return;

    if (ppu_interpret_write(offset, data) == 0x100)
    {
        printf("game tried to write but ppu blocked it\n");
        return;
    }
    
    if (mbc_interpret_write(offset, data) == 0x100)
        return;
    
    if (offset <= 0x7FFF) // don't allow writing to rom
        return;
    
    (* (byte *)map_to_physical_location(offset)) = data;
}

__always_inline void mem_write_16(uint16_t offset, word data) // simulating little endian byte order
{
    if (offset > 0xFFFE)
        offset = 0xFFFE;
    
    mem_write(offset, data.b.l);
    mem_write(offset + 1, data.b.h);
}

// todo: move redirection to rom into mbc implementations
__always_inline void *map_to_physical_location(uint16_t offset)
{
    offset = redirect_ram_echo(offset);

    if (offset < 0x4000)
        return rombuffer + offset;

    if (offset >= 0x4000 && offset <= 0x7FFF)
        return redirect_to_active_rom_bank(offset - 0x4000);
    
    if (offset >= 0xA000 && offset <= 0xBFFF)
        return redirect_to_active_cart_ram_bank(offset - 0xA000);
    
    return mem.raw + offset;
}

__always_inline uint16_t redirect_ram_echo(uint16_t offset)
{
    if (offset >= 0xE000 && offset <= 0xFDFF)
        offset -= 0x2000;
    
    return offset;
}

// todo: move into mbc implementations
__always_inline void *redirect_to_active_rom_bank(uint16_t offset)
{
    return rom_banks[active_rom_bank.w] + offset;
}

// todo: move into mbc implementations
__always_inline void *redirect_to_active_cart_ram_bank(uint16_t offset)
{
    return ext_ram_banks[active_ext_ram_bank.w] + offset;
}

void init_memory()
{    
    for (uint32_t i = 0; i < 0x10000; i++)
        mem.raw[i] = 0xFF;

    ext_chip_setup();

    active_rom_bank = word(1);
    active_ext_ram_bank = word(0);
}

