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


#include <nsgbe.h>
#include <sys/time.h>

#define SYSTEM_OVERCLOCK_MULTIPLIER 4

#define USEC_PER_SEC 1000000

#define MACHINE_CLOCK_HZ                (1048576 * (system_overclock ? SYSTEM_OVERCLOCK_MULTIPLIER : 1))
#define CPU_TICKS_PER_MACHINE_CLOCK     4
#define PPU_TICKS_PER_MACHINE_CLOCK     4
#define RAM_TICKS_PER_MACHINE_CLOCK     4
#define VRAM_TICKS_PER_MACHINE_CLOCK    2

#define CLOCK_TICKS_PER_SLEEP_CYCLE 1024
#define SLEEP_CYCLE_HZ (MACHINE_CLOCK_HZ / CLOCK_TICKS_PER_SLEEP_CYCLE)
#define USEC_PER_SLEEP_CYCLE (USEC_PER_SEC / SLEEP_CYCLE_HZ)

#define CLOCK_CYCLES_PER_CLOCK_TICK 1 // setting this value higher may result in bugs due to chip synchronisation, as they're all running on one thread
#define system_alive (cpu_alive && ppu_alive)

_Bool system_overclock = 0;  // indicates whether to apply SYSTEM_OVERCLOCK_MULTIPLIER to the base machine clock frequency
_Bool system_running = 0;    // indicates whether the system (clock) is running

int32_t cpu_clock_cycles_behind = 0; // negative means the cpu is in the future by given number of clock cycles
int32_t ppu_clock_cycles_behind = 0; // negative means the ppu is in the future by given number of clock cycles

void clock_tick_cpu_ppu()
{
    io_exec_cycles(1);
    cpu_clock_cycles_behind = cpu_exec_cycles(cpu_clock_cycles_behind + 1);
    ppu_clock_cycles_behind = ppu_exec_cycles(ppu_clock_cycles_behind + 1);
}

void clock_tick_machine()
{
    for (uint32_t c = 0; c < CPU_TICKS_PER_MACHINE_CLOCK; c++)
        clock_tick_cpu_ppu();
}

void system_run()
{    
    struct timeval tv;
    long time_pre, time_post, target_time;
    
    init_memory();
    cpu_reset();
    ppu_reset();

    system_running = 1;

    while (system_running && system_alive)
    {
        gettimeofday(&tv, NULL);
        time_pre = (USEC_PER_SEC * tv.tv_sec + tv.tv_usec);
        target_time = time_pre + USEC_PER_SLEEP_CYCLE;

        for (uint32_t c = 0; c < CLOCK_TICKS_PER_SLEEP_CYCLE; c++)
            clock_tick_machine();

        gettimeofday(&tv, NULL);
        time_post = (USEC_PER_SEC * tv.tv_sec + tv.tv_usec);

        // catch in this loop instead of sleeping
        // less efficient but a little more precise as it seems
        while (time_post < target_time)
        {
            gettimeofday(&tv, NULL);
            time_post = (USEC_PER_SEC * tv.tv_sec + tv.tv_usec);
        }
        
        //usleep(target_time - time_post);
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

