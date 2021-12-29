// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

// memory controller

#include <env.h>

union MEMORY mem;

void *map_to_physical_location(uint16_t offset);
uint16_t redirect_ram_echo(uint16_t offset);
void *redirect_to_active_rom_bank(uint16_t offset);
void *redirect_to_active_cart_ram_bank(uint16_t offset);
void *redirect_to_active_vram_bank(uint16_t offset);
void *redirect_to_active_wram_bank(uint16_t offset);

_Bool enable_bootrom = 0;
byte cgb_extra_vram_bank[0x2000];
byte cgb_extra_wram_banks[8][0x1000];

__always_inline byte mem_read(uint16_t offset)
{
    // < 0x100: continue; 0x1XX: return XX
    uint16_t component_response = 0;

    offset &= 0xFFFF;

    component_response = io_interpret_read(offset);
    if (component_response > 0xFF)
        return (component_response & 0xFF);

    if (enable_bootrom)
    {
        if (offset <= 0xFF)
            return *((byte *)biosbuffer + offset);

        if (gb_mode == MODE_CGB)
            if (offset >= 0x200 && offset <= 0x8FF)
                return *((byte *)biosbuffer + offset);
    }

    component_response = ppu_interpret_read(offset);
    if (component_response > 0xFF)
        return (component_response & 0xFF);

    component_response = mbc_interpret_read(offset);
    if (component_response > 0xFF)
        return (component_response & 0xFF);

    if (gb_mode == MODE_CGB)
    {
        if (offset == VBK)
            return (mem.raw[VBK] | 0xFE);
    }

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
        return;

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

    if (gb_mode == MODE_CGB)
    {
        if (offset >= 0x8000 && offset <= 0x9FFF)
            return redirect_to_active_vram_bank(offset - 0x8000);

        if (offset >= 0xD000 && offset <= 0xDFFF)
            return redirect_to_active_wram_bank(offset - 0xD000);
    }

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

__always_inline void *redirect_to_active_vram_bank(uint16_t offset)
{
    if (gb_mode != MODE_CGB)
        return mem.map.video_ram + offset;

    byte selected_vram_bank = mem.raw[VBK] & 0x1;

    switch (selected_vram_bank)
    {
        case 1:
            return cgb_extra_vram_bank + offset;
            break;

        default:
            return mem.map.video_ram + offset;
            break;
    }
}

__always_inline void *redirect_to_active_wram_bank(uint16_t offset)
{
    if (gb_mode != MODE_CGB)
        return mem.map.ram_bank_1 + offset;

    byte selected_wram_bank = mem.raw[0xFF70] & 0x7;

    if (selected_wram_bank == 0)
        selected_wram_bank = 1;

    if (selected_wram_bank == 1)
        return mem.map.ram_bank_1 + offset;

    return cgb_extra_wram_banks[selected_wram_bank] + offset;
}

void init_memory()
{
    memset(mem.map.rom_bank, 0xFF, 0x4000);
    memset(mem.map.rom_bank_s, 0xFF, 0x4000);
    memset(mem.map.video_ram, 0x00, 0x2000);
    memset(cgb_extra_vram_bank, 0x00, 0x2000);
    memset(mem.map.cart_ram_bank_s, 0x00, 0x2000);
    memset(mem.map.ram_bank_0, 0x00, 0x1000);
    memset(mem.map.ram_bank_1, 0x00, 0x1000); // pkmn tcg needs 0s here
    memset(mem.map.undefined, 0x00, 0x1E00);

    for (int i = 0; i < 8; i++)
        memset(cgb_extra_wram_banks[i], 0x00, 0x1000);

    memset(mem.map.sprite_attr_table, 0x00, 0xA0);
    memset(mem.map.prohibited, 0xFF, 0x60);
    memset(mem.map.dev_maps1, 0xFF, 0xF);
    mem.map.interrupt_flag_reg.b = 0x00;
    memset(mem.map.dev_maps2, 0xFF, 0x70);
    memset(mem.map.high_ram, 0xFF, 0x7F); // smb deluxe is stuck on black screen with 0s in here, so we init with 1s
    mem.map.interrupt_enable_reg.b = 0x00;

    ext_chip_setup();

    active_rom_bank = word(1);
    active_ext_ram_bank = word(0);
}
