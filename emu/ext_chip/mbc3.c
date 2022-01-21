// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

// mbc3 (duh)

#include <env.h>
#include <sys/time.h>

union RTC_DH {
    struct __attribute__((packed)) {
#ifdef __LITTLE_ENDIAN__
        _Bool day_high  : 1;
        byte unused     : 5;
        _Bool halt      : 1;
        _Bool day_carry : 1;
#else
        _Bool day_carry : 1;
        _Bool halt      : 1;
        byte unused     : 5;
        _Bool day_high  : 1;
#endif
    };
    byte b;
} rtc_dh;

extern _Bool ext_ram_enabled;

_Bool rtc_reg_selected = 0;
uint8_t selected_rtc_reg = 0x8;
_Bool latching = 0;

struct timeval tv;
time_t initial_tv_seconds;
time_t time_elapsed;

uint16_t mbc3_interpret_write(uint16_t offset, byte data)
{
    if (offset <= 0x1FFF)
    {
        if (data == 0xA0)
            ext_ram_enabled = 1;
        else if (data == 0x00)
            ext_ram_enabled = 0;

        return 0x100;
    }

    if (offset >= 0x2000 && offset <= 0x3FFF)
    {
        if (data == 0)
            data = 1;

        data &= 0x7F;

        active_rom_bank.w = data;

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

        if (data <= 3)
        {
            rtc_reg_selected = 0;
            active_ext_ram_bank.w = data;
        }
        else
        {
            rtc_reg_selected = 1;
            selected_rtc_reg = data;
        }

        return 0x100;
    }

    if (offset >= 0x6000 && offset <= 0x7FFF && rtc_reg_selected)
    {
        if (data == 0 && !latching)
            latching = 1;

        if (data == 1 && latching)
        {
            latching = 0;
            gettimeofday(&tv, NULL);
            time_elapsed = tv.tv_sec - initial_tv_seconds;
            rtc_dh.day_high = (((time_elapsed / 3600) / 24) > 0xFF ? 1 : 0);
        }

        return 0x100;
    }

    // if (offset >= 0xA000 && offset <= 0xBFFF && !ext_ram_enabled)
        // return 0x100;

    if (offset >= 0xA000 && offset <= 0xBFFF && rtc_reg_selected)
    {
        if (selected_rtc_reg == 0xC)
            rtc_dh.b = data;

        return 0x100;
    }

    return 0;
}

uint16_t mbc3_interpret_read(uint16_t offset)
{
    //if (offset >= 0xA000 && offset <= 0xBFFF && !ext_ram_enabled)
        //return 0x1FF;

    if (offset >= 0xA000 && offset <= 0xBFFF && rtc_reg_selected)
    {
        switch (selected_rtc_reg)
        {
            case 0x8: // seconds
                return 0x100 | (time_elapsed % 60);
                break;

            case 0x9: // minutes
                return 0x100 | ((time_elapsed / 60) % 60);
                break;

            case 0xA: // hours
                return 0x100 | ((time_elapsed / 3600) % 24);
                break;

            case 0xB: // days (low)
                return 0x100 | ((time_elapsed / 86400) & 0xFF);
                break;

            case 0xC: // days (high), carry bit, halt flag
                return 0x100 | rtc_dh.b;
                break;

            default:
                return 0x100;
                break;
        }
    }

    return 0;
}

uint32_t mbc3_setup()
{
    printf("[Info] Using MBC3.\n");

    for (uint16_t i = 1; i < ext_ram_bank_count; i++)
        free_ptr(&ext_ram_banks[i]);

    free_ptr(&ext_ram_banks);

    active_mbc_writes_interpreter = &mbc3_interpret_write;
    active_mbc_reads_interpreter = &mbc3_interpret_read;

    ext_ram_bank_count = 4;
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

    gettimeofday(&tv, NULL);
    initial_tv_seconds = tv.tv_sec; // todo: load this from save file instead of setting it to the current time
    time_elapsed = 0;

    return 0;
}
