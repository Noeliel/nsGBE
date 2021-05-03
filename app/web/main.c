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


#include <signal.h>
#include <stdlib.h>
#include <pthread.h>

#include <env.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#define NSGBE_STORAGE_PREFIX "/nsGBE/"
#define NSGBE_STORAGE_SUFFIX ".sav"

int gui_main();

char *rompath;
char *biospath;
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

int main(int argc, char **argv) {

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
