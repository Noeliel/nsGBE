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


/* nsGBE - no special Game Boy Emulator */

#include "env.h"

_Bool do_print_debug = 1;

void *biosbuffer;
uintptr_t biossize;

void *rombuffer;
uintptr_t romsize;
struct ROM_HEADER *rom_header;

char *rom_path;
char *battery_path;

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

int bios_load(char *path)
{
    // load file
    
    FILE *fbuf = fopen(path, "r");
    
    if (!fbuf)
    {
        printf("Error trying to open bios file: %s\n", path);
        return 0;
    }
    
    fseek(fbuf, 0, SEEK_END);
    biossize = ftell(fbuf);
    rewind(fbuf);
    
    biosbuffer = malloc(biossize);
    if (!fread(biosbuffer, biossize, 1, fbuf))
    {
        printf("Error trying to read bios file: %s\n", path);
        return 0;
    }
    
    fclose(fbuf);

    return 1;
}

__always_inline void free_ptr(void **ptr)
{
    if (*ptr)
    {
        free(*ptr);
        *ptr = 0;
    }
}

int rom_load(char *path)
{
    // load file

    free_ptr(&rom_path);
    free_ptr(&battery_path);

    size_t path_length = strlen(path);

    rom_path = malloc(path_length + 1);
    strncpy(rom_path, path, path_length + 1);
    
    battery_path = malloc(path_length + 4 + 1);

    memcpy(battery_path, rom_path, path_length + 1);
    strcat(battery_path, ".sav");

    FILE *fbuf = fopen(rom_path, "r");
    
    if (!fbuf)
    {
        printf("Error trying to open rom file: %s\n", rom_path);
        return 0;
    }
    
    fseek(fbuf, 0, SEEK_END);
    romsize = ftell(fbuf);
    rewind(fbuf);
    
    rombuffer = malloc(romsize);
    if (!fread(rombuffer, romsize, 1, fbuf))
    {
        printf("Error trying to read rom file: %s\n", rom_path);
        return 0;
    }
    
    fclose(fbuf);
    
    rom_header = rombuffer + 0x100;

    printf("\n");
    printf("nsGBE - no special Game Boy Emulator\n");
    printf("rev. %s (%s branch)\n", GIT_HASH, GIT_BRANCH);
    printf("------------------------------------\n");
    // printf("Start vector: 0x%02x 0x%02x 0x%02x 0x%02x\n", rom_header->start_vector[0], rom_header->start_vector[1], rom_header->start_vector[2], rom_header->start_vector[3]);
    printf("Rom size: %lu byte\n", romsize);
    printf("Rom title: %s\n", rom_header->game_title);
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
    char *path = battery_path;

    // load file
    
    FILE *fbuf = fopen(path, "r");
    
    if (!fbuf)
    {
        printf("Error trying to open battery file: %s\n", path);
        return;
    }
    
    fseek(fbuf, 0, SEEK_END);
    uint32_t battery_size = ftell(fbuf);
    rewind(fbuf);
    
    void *battery_buffer = malloc(battery_size);
    if (!fread(battery_buffer, battery_size, 1, fbuf))
    {
        printf("Error trying to read battery file: %s\n", path);
        return;
    }
    
    for (uint16_t i = 0; i < bank_count; i++)
        for (uint16_t field = 0; field < 0x2000; field++)
            battery_banks[i][field] = ((byte *)battery_buffer)[field + (i * 0x2000)];
    
    fclose(fbuf);
    free(battery_buffer);
}

void battery_save(byte **battery_banks, uint16_t bank_count)
{
    char *path = battery_path;

    byte *battery_buffer = malloc(0x2000 * bank_count);

    for (uint16_t i = 0; i < bank_count; i++)
        for (uint16_t field = 0; field < 0x2000; field++)
            battery_buffer[field + (i * 0x2000)] = battery_banks[i][field];

    FILE *fbuf = fopen(path, "w");
    if (!fbuf)
    {
        printf("Error trying to open battery file: %s\n", path);
        return;
    }
    
    fwrite(battery_buffer, 0x2000, bank_count, fbuf);
    
    fclose(fbuf);
    free(battery_buffer);
}

void write_battery()
{
    if (battery_enabled)
        battery_save(ext_ram_banks, ext_ram_bank_count);
}