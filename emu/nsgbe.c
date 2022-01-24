// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

/* nsGBE - no special Game Boy Emulator */

#include <env.h>

uint8_t *biosbuffer;
uintptr_t biossize;

uint8_t *rombuffer;
uintptr_t romsize;
struct ROM_HEADER *rom_header;

enum GB_MODE gb_mode;

static byte calc_header_checksum(void *rombuffer)
{
    byte checksum = 0;
    uint32_t offset = 0x0134;

    while (offset <= 0x014C)
    {
        checksum = checksum - *(byte *)(rombuffer + (offset++)) - 1;
    }

    return checksum;
}

__always_inline void free_ptr(void **ptr)
{
    if (*ptr)
    {
        free(*ptr);
        *ptr = 0;
    }
}

static int bios_load()
{
    // load file

    free_ptr((void **)&biosbuffer);

    biossize = load_bios(&biosbuffer);

    if (biossize == 0)
    {
        printf("Failed to load bios file.\n");
        return 0;
    }

    return 1;
}

static int rom_load()
{
    // load file

    free_ptr((void **)&rombuffer);

    romsize = load_rom(&rombuffer);

    if (romsize == 0)
    {
        printf("Failed to load rom file.\n");
        return 0;
    }

    rom_header = (struct ROM_HEADER *)(rombuffer + 0x100);

    printf("\n");
    printf("nsGBE - no special Game Boy Emulator\n");
#ifdef GIT_HASH
#ifdef GIT_BRANCH
    printf("rev. %s (%s branch)\n", GIT_HASH, GIT_BRANCH);
#endif
#endif
    printf("------------------------------------\n");
    // printf("Start vector: 0x%02x 0x%02x 0x%02x 0x%02x\n", rom_header->start_vector[0], rom_header->start_vector[1], rom_header->start_vector[2], \
      rom_header->start_vector[3]);
    printf("Rom size: %lu byte\n", romsize);
    printf("Rom title: %.*s\n", 15, rom_header->game_title);
    printf("Destination code: 0x%02X\n", rom_header->destination_code);
    printf("Cartridge type: 0x%02X\n", rom_header->cartridge_type);
    printf("GBC flag: 0x%02X\n", rom_header->gbc_flag);
    printf("SGB flag: 0x%02X\n", rom_header->sgb_flag);
    printf("Header checksum is %s\n", (rom_header->header_checksum == calc_header_checksum(rombuffer) ? "valid" : "invalid"));
    printf("------------------------------------\n\n");

    return 1;
}

void battery_load(byte **battery_banks, uint16_t bank_count)
{
    uint8_t *battery_buffer;
    long battery_size;

    // load file

    battery_size = load_battery(&battery_buffer);

    if (battery_size == 0)
    {
        printf("Failed to load battery file.\n");
        return;
    }

    for (uint16_t i = 0; i < bank_count; i++)
        for (uint16_t field = 0; field < 0x2000; field++)
            battery_banks[i][field] = ((byte *)battery_buffer)[field + (i * 0x2000)];

    free(battery_buffer);
}

static void battery_save(byte **battery_banks, uint16_t bank_count)
{
    byte *battery_buffer = malloc(0x2000 * bank_count);

    for (uint16_t i = 0; i < bank_count; i++)
        for (uint16_t field = 0; field < 0x2000; field++)
            battery_buffer[field + (i * 0x2000)] = battery_banks[i][field];

    if (save_battery(battery_buffer, 0x2000 * bank_count) == 0)
        printf("Failed to write battery file.\n");

    free(battery_buffer);
}

void write_battery()
{
    if (battery_enabled)
        battery_save(ext_ram_banks, ext_ram_bank_count);
}

void system_reset()
{
    if (!rom_load())
        return;

    if (rom_header->gbc_flag == 0xC0)
    {
        gb_mode = MODE_CGB;

        printf("This game is a Game Boy Color exclusive.\n");
        printf("Please note that support for some GBC-specific features is experimental, others are missing completely.\n");
        printf("Expect breakage.\n");
    }

#if PREFER_CGB_MODE
    if (rom_header->gbc_flag == 0x80)
        gb_mode = MODE_CGB;
#endif

    // if (!bios_load())
        // return;

    init_memory();
    cpu_reset();
    ppu_reset();

    if (gb_mode == MODE_CGB)
        fake_cgb_bootrom();
    else
        fake_dmg_bootrom();
}

int system_run_event_loop()
{
    system_resume();
    clock_loop();

    return 0;
}
