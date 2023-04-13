// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <nsgbe.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#define NSGBE_STORAGE_PREFIX "/nsGBE/"
#define NSGBE_STORAGE_SUFFIX ".sav"

int gui_main();

char *rompath = NULL;
char *biospath = NULL;
char batterypath[32];

__always_inline void free_ptr(void **ptr)
{
    if (*ptr)
    {
        free(*ptr);
        *ptr = 0;
    }
}

static long file_read(uint8_t **buffer, char *path)
{
    FILE *fbuf = fopen(path, "rb");

    if (!fbuf)
    {
        printf("Error trying to open file: %s\n", path);
        return 0;
    }

    fseek(fbuf, 0, SEEK_END);
    long fsize = ftell(fbuf);
    rewind(fbuf);

    *buffer = malloc(fsize);
    if (!fread(*buffer, fsize, 1, fbuf))
    {
        printf("Error trying to read file: %s\n", path);
        return 0;
    }

    fclose(fbuf);

    return fsize;
}

static int file_write(char *path, uint8_t *buffer, size_t size)
{
    FILE *fbuf = fopen(path, "w+");
    if (!fbuf)
    {
        printf("Error trying to open file: %s\n", path);
        return 0;
    }

    size_t num = fwrite(buffer, size, 1, fbuf);

    fclose(fbuf);

    EM_ASM_({
        FS.syncfs(function (err) {
            Module.print("Successfully persisted save data!");
        });
    });

    return num;
}

long load_rom(uint8_t **buffer)
{
    rompath = "/rom.gb";

    return file_read(buffer, rompath);
}

long load_bios(uint8_t **buffer)
{
    return file_read(buffer, biospath);
}

long load_battery(uint8_t **buffer)
{
    strcat(batterypath, NSGBE_STORAGE_PREFIX);
    strncat(batterypath, rom_header->game_title, 15);
    batterypath[22] = 0;
    strcat(batterypath, NSGBE_STORAGE_SUFFIX);

    return file_read(buffer, batterypath);
}

int save_battery(uint8_t *buffer, size_t size)
{
    return file_write(batterypath, buffer, size);
}

static void catch_exit(int signal_num)
{
    write_battery();
    exit(0);
}

extern void sdl_renderloop();
void system_prepare()
{
    system_reset();
    emscripten_set_main_loop(sdl_renderloop, 0, 0);
}

int main(int argc, char **argv)
{
    EM_ASM_({
        FS.mkdir('/nsGBE');
        FS.mount(IDBFS, {}, '/nsGBE');

        Module.fs_init_finished = 0;

        FS.syncfs(true, function (err) {
            Module.fs_init_finished = 1;
        });
    });

    gui_main();

    return 0;
}
