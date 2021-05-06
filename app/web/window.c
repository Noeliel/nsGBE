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

#include <SDL2/SDL.h>
#include <nsgbe.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#define WINDOW_TITLE_FORMATTER "[ nsGBE ] [ %d fps ]"

#define SCREEN_SCALE 3

#define SYSTEM_OVERCLOCK_MULTIPLIER     4
#define MACHINE_CLOCK_HZ                1048576 // original DMG
#define EFFECTIVE_MACHINE_CLOCK_HZ      (MACHINE_CLOCK_HZ * (system_overclock ? SYSTEM_OVERCLOCK_MULTIPLIER : 1))
#define CLOCK_TICKS_PER_SLEEP_CYCLE     1024
#define SLEEP_CYCLE_HZ (EFFECTIVE_MACHINE_CLOCK_HZ / CLOCK_TICKS_PER_SLEEP_CYCLE)

uint32_t *framebuffer;

char title_buffer[32];
uint16_t last_framecounter = 0;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *buffer;

union BUTTON_STATE button_states;

void set_canvas_scale()
{
    EM_ASM_({
        let context = Module['canvas'].getContext('2d');
        context.scale(3.0, 3.0);
        context.imageSmoothingEnabled = false;
        context.webkitImageSmoothingEnabled = false;
        context.mozImageSmoothingEnabled = false;
    });
}

void copy_to_canvas(uint32_t *buffer, int w, int h)
{
    EM_ASM_({
        let data = Module.HEAPU8.slice($0, $0 + $1 * $2 * 4);

        let smallCanvas = document.getElementById("smallcanvas");
        let smallContext = smallCanvas.getContext("2d");
        let imageData = smallContext.getImageData(0, 0, $1, $2);
        imageData.data.set(data);
        smallContext.putImageData(imageData, 0, 0);

        let canvas = Module['canvas'];
        let context = canvas.getContext('2d');
        context.drawImage(smallCanvas, 0, 0);
    }, buffer, w, h);
}

void vblank()
{
    framebuffer = display_request_next_frame();
    copy_to_canvas(framebuffer, GB_FRAMEBUFFER_WIDTH, GB_FRAMEBUFFER_HEIGHT);
}

long time_start;
uint16_t framecounter = 0;
void handle_vblank()
{
    struct timeval tv;
    long time_now;

    gettimeofday(&tv, NULL);
    time_now = (1000000 * tv.tv_sec + tv.tv_usec);

    if (time_start == 0)
        time_start = time_now;

    if (time_now - time_start > 1000000)
    {
        time_start = time_now;
        last_framecounter = framecounter;
        framecounter = 0;
    }
    else
    {
        framecounter++;
    }

    vblank();
}

static void handleKeyDown(SDL_KeyboardEvent key)
{
    switch (key.keysym.scancode)
    {
        case SDL_SCANCODE_SPACE:
            system_overclock = 1;
            break;

        case SDL_SCANCODE_K:
            button_states.A = 1;
            break;

        case SDL_SCANCODE_O:
            button_states.B = 1;
            break;

        case SDL_SCANCODE_L:
            button_states.START = 1;
            break;

        case SDL_SCANCODE_P:
            button_states.SELECT = 1;
            break;

        case SDL_SCANCODE_W:
            button_states.UP = 1;
            break;

        case SDL_SCANCODE_S:
            button_states.DOWN = 1;
            break;

        case SDL_SCANCODE_A:
            button_states.LEFT = 1;
            break;

        case SDL_SCANCODE_D:
            button_states.RIGHT = 1;
            break;

        default:
            break;
    }
}

static void handleKeyUp(SDL_KeyboardEvent key)
{
    switch (key.keysym.scancode)
    {
        case SDL_SCANCODE_SPACE:
            system_overclock = 0;
            break;

        case SDL_SCANCODE_K:
            button_states.A = 0;
            break;

        case SDL_SCANCODE_O:
            button_states.B = 0;
            break;

        case SDL_SCANCODE_L:
            button_states.START = 0;
            break;

        case SDL_SCANCODE_P:
            button_states.SELECT = 0;
            break;

        case SDL_SCANCODE_W:
            button_states.UP = 0;
            break;

        case SDL_SCANCODE_S:
            button_states.DOWN = 0;
            break;

        case SDL_SCANCODE_A:
            button_states.LEFT = 0;
            break;

        case SDL_SCANCODE_D:
            button_states.RIGHT = 0;
            break;

        case SDL_SCANCODE_B:
            write_battery();
            break;

        default:
            break;
    }
}

void sdl_renderloop()
{
    _Bool quit = 0;
    SDL_Event e;

    if (!EM_ASM_INT({ return Module.fs_init_finished; }))
        return;

    for (int i = 0; i < (SLEEP_CYCLE_HZ / 60); i++)
    {
        clock_perform_sleep_cycle_ticks();

        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                quit = 1;

            if (e.type == SDL_KEYDOWN)
                handleKeyDown(e.key);

            if (e.type == SDL_KEYUP)
                handleKeyUp(e.key);
        }
    }

    sprintf(title_buffer, WINDOW_TITLE_FORMATTER, last_framecounter);
    SDL_SetWindowTitle(window, title_buffer);
}

int gui_main()
{
    SDL_Init(SDL_INIT_EVENTS);

    window = SDL_CreateWindow("[ nsGBE ]", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, GB_FRAMEBUFFER_WIDTH * SCREEN_SCALE, GB_FRAMEBUFFER_HEIGHT * SCREEN_SCALE, 0);

    display_notify_vblank = &handle_vblank;

    set_canvas_scale();

    return 0;
}

void sdl_quit()
{
    write_battery();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
