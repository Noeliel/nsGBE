// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

// mbc and other peripherals

#include <env.h>

byte **rom_banks/*[0x4000]*/;
byte **ext_ram_banks/*[0x2000]*/;
word active_rom_bank = word(0x0001);
word active_ext_ram_bank = word(0x0000); // most games only have up to 256 rom banks, still there are some with more, hence using word

uint16_t rom_bank_count = 2;
uint16_t ext_ram_bank_count = 1;

_Bool ext_ram_enabled = 1;
_Bool battery_enabled = 0;

uint16_t (* active_mbc_writes_interpreter)(uint16_t offset, byte data);
uint16_t (* active_mbc_reads_interpreter)(uint16_t offset);

uint16_t generic_mbc_interpret_write(uint16_t offset, byte data);
uint16_t generic_mbc_interpret_read(uint16_t offset);

static void init_random_ram() // todo: better; currently not in use
{
    for (uint16_t i = 0; i < 0x1000; i++)
    {
        mem.map.ram_bank_0[i] = (i + 1) & 0xFF;
        mem.map.ram_bank_0[i] = (i - 1) & 0xFF;
    }

    for (uint16_t i = 0; i < 0x2000; i++)
    {
        mem.map.cart_ram_bank_s[i] = (i + 1) & 0xFF;
        mem.map.video_ram[i] = (i - 1) & 0xFF;
    }

    for (uint16_t i = 0; i < 0x80; i++)
        mem.map.high_ram[i] = (i + 1) & 0xFF;
}

uint32_t no_mbc_setup()
{
    for (uint16_t i = 1; i < ext_ram_bank_count; i++)
        free_ptr(&ext_ram_banks[i]);

    free_ptr(&ext_ram_banks);

    ext_ram_bank_count = 1;
    ext_ram_banks = malloc((size_t)(ext_ram_bank_count * sizeof(byte *)));
    ext_ram_banks[0] = mem.map.cart_ram_bank_s;
}

int ext_chip_setup()
{
    free_ptr(&rom_banks);

    active_mbc_writes_interpreter = &generic_mbc_interpret_write;
    active_mbc_reads_interpreter = &generic_mbc_interpret_read;

    switch (rom_header->ram_size)
    {

    }

    switch (rom_header->cartridge_type)
    {
        case 0x00:
            // no mbc
            no_mbc_setup();
            break;

        //case 0x01:
            // mbc1
            //break;

        //case 0x02:
            // mbc1 + ram
            //break;

        //case 0x03:
            // mbc1 + ram + battery
            //break;

        //case 0x05:
            // mbc2
            //break;

        //case 0x06:
            // mbc2 + ram + battery
            //break;

        //case 0x08:
            // rom + ram
            //break;

        //case 0x09:
            // rom + ram + battery
            //break;

        //case 0x0B:
            // mmm01
            //break;

        //case 0x0C:
            // mmm01 + ram
            //break;

        //case 0x0D:
            // mmm01 + ram + battery
            //break;

        //case 0x0F:
            // mbc3 + timer + battery
            //break;

        case 0x10:
            // mbc3 + ram + timer + battery
            battery_enabled = 1;
            mbc3_setup();
            break;

        //case 0x11:
            // mbc3
            //break;

        //case 0x12:
            // mbc3 + ram
            //break;

        case 0x13:
            // mbc3 + ram + battery
            battery_enabled = 1;
            mbc3_setup();
            break;

        //case 0x19:
            // mbc5
            //break;

        //case 0x1A:
            // mbc5 + ram
            //break;

        case 0x1B:
            // mbc5 + ram + battery
            battery_enabled = 1;
            mbc5_setup();
            break;

        //case 0x1C:
            // mbc5 + rumble
            //break;

        //case 0x1D:
            // mbc5 + ram + rumble
            //break;

        //case 0x1E:
            // mbc5 + ram + battery + rumble
            //break;

        //case 0x20:
            // mbc6 + ram + battery
            //break;

        //case 0x22:
            // mbc7 + ram + battery + accelerometer
            //break;

        //case 0xFC:
            // pocket camera
            //break;

        //case 0xFD:
            // bandai tama5
            //break;

        //case 0xFE:
            // huc3
            //break;

        //case 0xFF:
            // huc1 + ram + battery
            //break;

        default:
            printf("Unsupported cartridge type!\n\n");
            exit(0);
            break;

    }

    rom_bank_count = romsize / 0x4000;

    rom_banks = malloc((size_t)(rom_bank_count * sizeof(byte *)));

    for (uint16_t i = 0; i < rom_bank_count; i++)
    {
        rom_banks[i] = (byte *)(rombuffer + (i * 0x4000));
    }

    //init_random_ram();

    return 0;
}

uint16_t mbc_interpret_write(uint16_t offset, byte data)
{
    return active_mbc_writes_interpreter(offset, data);
}

uint16_t mbc_interpret_read(uint16_t offset)
{
    return active_mbc_reads_interpreter(offset);
}

__always_inline uint16_t generic_mbc_interpret_write(uint16_t offset, byte data)
{
    if (offset >= 0x0000 && offset <= 0x1FFF)
    {
        if ((data & 0xFF) == 0x0A) // RAM enable
            // TODO: enable RAM
            return 0x100;
        else // RAM disable
            // TODO: disable RAM
            return 0x100;
    }

    if (offset >= 0x2000 && offset <= 0x3FFF)
    {
        // switch ROM bank

        if (data >= rom_bank_count)
        {
            printf("error: selected rombank (0x%04X) oob (have 0x%04X)\n", active_rom_bank.w, rom_bank_count);
            cpu_break();
        }

        active_rom_bank = word(data);

        return 0x100;
    }

    return 0;
}

__always_inline uint16_t generic_mbc_interpret_read(uint16_t offset)
{
    if (offset >= 0xA000 && offset <= 0xBFFF && !ext_ram_enabled)
        return 0x1FF; // external ram is disabled

    return 0;
}
