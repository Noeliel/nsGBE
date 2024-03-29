// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

#include "env.h"
#include <sys/time.h>
#include <unistd.h>

#define SYSTEM_OVERCLOCK_MULTIPLIER     4
#define EFFECTIVE_MACHINE_CLOCK_HZ      (MACHINE_CLOCK_HZ * (system_overclock ? SYSTEM_OVERCLOCK_MULTIPLIER : 1))

#define CLOCK_TICKS_PER_SLEEP_CYCLE     1024
#define SLEEP_CYCLE_HZ                  (EFFECTIVE_MACHINE_CLOCK_HZ / CLOCK_TICKS_PER_SLEEP_CYCLE)
#define USEC_PER_SLEEP_CYCLE            (USEC_PER_SEC / SLEEP_CYCLE_HZ)

#define CLOCK_CYCLES_PER_CLOCK_TICK     1 // setting this value higher may result in bugs due to chip synchronisation, as they're all running on one thread

#define system_alive (cpu_alive && ppu_alive)

_Bool system_overclock = 0;  // indicates whether to apply SYSTEM_OVERCLOCK_MULTIPLIER to the base machine clock frequency
_Bool system_running = 0;    // indicates whether the system (clock) is running

int32_t cpu_clock_cycles_behind = 0; // negative means the cpu is in the future by given number of clock cycles
int32_t ppu_clock_cycles_behind = 0; // negative means the ppu is in the future by given number of clock cycles

__always_inline static void clock_tick_cpu_ppu()
{
    io_exec_cycles(1);
    cpu_clock_cycles_behind = cpu_exec_cycles(cpu_clock_cycles_behind + 1);
    ppu_clock_cycles_behind = ppu_exec_cycles(ppu_clock_cycles_behind + 1);
}

__always_inline static void clock_tick_machine()
{
    for (uint32_t c = 0; c < CPU_TICKS_PER_MACHINE_CLOCK; c++)
        clock_tick_cpu_ppu();
}

__always_inline void clock_perform_sleep_cycle_ticks()
{
    for (uint32_t c = 0; c < CLOCK_TICKS_PER_SLEEP_CYCLE; c++)
    {
        if (!system_running)
            break;

        clock_tick_machine();
    }
}

uint32_t time_pre;
__always_inline void clock_perform_sleep_cycle()
{
    struct timeval tv;
    uint32_t time_now, target_time;

    target_time = time_pre + USEC_PER_SLEEP_CYCLE;

    gettimeofday(&tv, NULL);
    time_now = (USEC_PER_SEC * tv.tv_sec + tv.tv_usec);

    uint32_t target = (target_time - time_now) - 20;
    if (target < USEC_PER_SLEEP_CYCLE)
    {
        usleep(target);

        gettimeofday(&tv, NULL);
        time_now = (USEC_PER_SEC * tv.tv_sec + tv.tv_usec);
    }
    else
        target_time = time_now;

    while (system_running && time_now < target_time)
    {
        usleep(1);

        gettimeofday(&tv, NULL);
        time_now = (USEC_PER_SEC * tv.tv_sec + tv.tv_usec);
    }

    time_pre = target_time;

    clock_perform_sleep_cycle_ticks();
}

void clock_loop()
{
    while (system_alive)
    {
        while (system_running && system_alive)
            clock_perform_sleep_cycle();

        usleep(100);
    }

    if (!system_alive)
    {
        printf("--------------------------------------------------------------------------\n");
        printf("A critical component stopped executing, forcing the system to shut down...\n");
        printf("System overview:\n");
        printf("CPU alive: %s, PC: 0x%04X\n", (cpu_alive ? "Yes" : "No"), cpu_regs.PC);
        printf("PPU alive: %s, Mode: %d\n", (ppu_alive ? "Yes" : "No"), ppu_regs.stat->mode);
        printf("--------------------------------------------------------------------------\n");
    }
}

void system_resume()
{
    system_running = 1;
}

void system_pause()
{
    system_running = 0;
}
