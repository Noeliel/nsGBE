// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

// mbc5 (duh)

#include <env.h>

extern _Bool ext_ram_enabled;

uint16_t mbc5_interpret_write(uint16_t offset, byte data)
{
    if (offset >= 0x0000 && offset <= 0x1FFF)
    {
        if ((data & 0x0F) == 0x0A) // RAM enable
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

    if (offset >= 0xA000 && offset <= 0xBFFF && !ext_ram_enabled)
        return 0x100;

    return 0;
}

uint16_t mbc5_interpret_read(uint16_t offset)
{
    return 0;
}

uint32_t mbc5_setup()
{
    printf("[Info] Using MBC5.\n");

    for (uint16_t i = 1; i < ext_ram_bank_count; i++)
        free_ptr(&ext_ram_banks[i]);

    free_ptr(&ext_ram_banks);

    active_mbc_writes_interpreter = &mbc5_interpret_write;
    active_mbc_reads_interpreter = &mbc5_interpret_read;

    ext_ram_bank_count = 0x10;
    ext_ram_banks = malloc((size_t)(ext_ram_bank_count * sizeof(byte *)));
    ext_ram_banks[0] = mem.map.cart_ram_bank_s;

    for (uint16_t i = 1; i < ext_ram_bank_count; i++)
    {
        ext_ram_banks[i] = malloc(0x2000);
        memset(ext_ram_banks[i], 0x00, 0x2000);
    }

    ext_ram_enabled = 1;
    active_ext_ram_bank.w = 0;

    if (battery_enabled)
        battery_load(ext_ram_banks, ext_ram_bank_count);

    return 0;
}
