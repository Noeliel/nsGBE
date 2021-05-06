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
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <nsgbe.h>

char *rompath;
char *biospath;
char *batterypath;

extern int gui_main(int argc, char **argv);

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
    FILE *fbuf = fopen(path, "wb");
    if (!fbuf)
    {
        printf("Error trying to open file: %s\n", path);
        return 0;
    }

    size_t num = fwrite(buffer, size, 1, fbuf);

    fclose(fbuf);

    return num;
}

long load_rom(uint8_t **buffer)
{
    free_ptr(&batterypath);

    size_t path_length = strlen(rompath);

    batterypath = malloc(path_length + 4 + 1);

    memcpy(batterypath, rompath, path_length + 1);
    strcat(batterypath, ".sav");

    return file_read(buffer, rompath);
}

long load_bios(uint8_t **buffer)
{
    return file_read(buffer, biospath);
}

long load_battery(uint8_t **buffer)
{
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

int main(int argc, char **argv) {

    pthread_t core_thread;
    pthread_attr_t core_thread_attributes;

    if (argc != 2)
        exit(0);

    if (signal(SIGTERM, catch_exit) == SIG_ERR) {
        printf("Failed to set up SIGTERM handler.\n");
        return EXIT_FAILURE;
    }

    if (signal(SIGINT, catch_exit) == SIG_ERR) {
        printf("Failed to set up SIGINT handler.\n");
        return EXIT_FAILURE;
    }

    if (signal(SIGABRT, catch_exit) == SIG_ERR) {
        printf("Failed to set up SIGABRT handler.\n");
        return EXIT_FAILURE;
    }

    rompath = argv[1];
    system_reset();

#define LAUNCH_WITH_GUI 1
#ifdef LAUNCH_WITH_GUI
    pthread_attr_init(&core_thread_attributes);
    pthread_create(&core_thread, &core_thread_attributes, system_run_event_loop, NULL);
    gui_main(1, argv);
#else
    system_run_event_loop();
#endif

    return 0;
}
