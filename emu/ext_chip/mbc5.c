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


// mbc5 (duh)

#include "../env.h"

extern _Bool ext_ram_enabled;

uint16_t mbc5_interpret_write(unsigned short offset, byte data)
{
    if (offset >= 0x0000 && offset <= 0x1FFF)
    {
        if (data & 0xFF == 0x0A) // RAM enable
            ext_ram_enabled = 1;
        else if (data == 0x00) // RAM disable
            ext_ram_enabled = 0;

        return 0x100;
    }

    if (offset >= 0x2000 && offset <= 0x2FFF)
    {
        // switch ROM bank (least significant 8 bit)

        active_rom_bank.w = (active_rom_bank.w & 0xFF00) + data;

        if (active_rom_bank.w >= rom_bank_count)
        {
            printf("error: selected rombank (0x%04X) oob (have 0x%04X)\n", active_rom_bank.w, rom_bank_count);
            cpu_break();
        }
    
        return 0x100;
    }

    if (offset >= 0x3000 && offset <= 0x3FFF)
    {
        // switch ROM bank (9th bit)

        active_rom_bank.w = ((active_rom_bank.w & 0x00FF) + ((data & 1) << 8));
    
        if (active_rom_bank.w >= rom_bank_count)
        {
            printf("error: selected rombank (0x%04X) oob (have 0x%04X)\n", active_rom_bank.w, rom_bank_count);
            cpu_break();
        }

        return 0x100;
    }

    if (offset >= 0x4000 && offset <= 0x5FFF)
    {
        // switch ext RAM bank
        data &= 0xF;

        active_ext_ram_bank.w = data;

        return 0x100;
    }

    //if (offset >= 0xA000 && offset <= 0xBFFF && !ext_ram_enabled)
        //return 0xFF;

    return 0;
}

uint16_t mbc5_interpret_read(unsigned short offset)
{    
    return 0;   
}

uint32_t mbc5_setup()
{
    active_mbc_writes_interpreter = &mbc5_interpret_write;
    active_mbc_reads_interpreter = &mbc5_interpret_read;

    ext_ram_bank_count = 0x10;
    ext_ram_banks = malloc((size_t)(ext_ram_bank_count * sizeof(byte *)));
    ext_ram_banks[0] = mem.map.cart_ram_bank_s;

    for (uint16_t i = 1; i < ext_ram_bank_count; i++)
        ext_ram_banks[i] = malloc(0x2000);

    ext_ram_enabled = 1;

    if (battery_enabled)
        battery_load(ext_ram_banks, ext_ram_bank_count);

    return 0;
}
