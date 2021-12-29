// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

#include <SDL2/SDL.h>
#include <nsgbe.h>

#define WINDOW_TITLE_FORMATTER "[ nsGBE ] [ %d fps ]"

#define SCREEN_SCALE 3

uint32_t *framebuffer;

char title_buffer[32];
uint16_t last_framecounter = 0;

SDL_Window *window;
SDL_Renderer *renderer;

union BUTTON_STATE button_states;

__always_inline void vblank()
{
    framebuffer = display_request_next_frame();

    SDL_RenderClear(renderer);

    for (int y = 0; y < GB_FRAMEBUFFER_HEIGHT; y++)
    {
        for (int x = 0; x < GB_FRAMEBUFFER_WIDTH; x++)
        {
            uint32_t color = *(framebuffer + (y * GB_FRAMEBUFFER_WIDTH + x));
            uint8_t color_b = (color >> 16) & 0xFF;
            uint8_t color_g = (color >> 8) & 0xFF;
            uint8_t color_r = color & 0xFF;

            struct SDL_Rect rect;
            rect.x = x * SCREEN_SCALE;
            rect.y = y * SCREEN_SCALE;
            rect.w = SCREEN_SCALE;
            rect.h = SCREEN_SCALE;

            SDL_SetRenderDrawColor(renderer, color_r, color_g, color_b, 0xFF);
            SDL_RenderFillRect(renderer, &rect);
        }
    }

    SDL_RenderPresent(renderer);
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

#ifdef __DEBUG
        case SDL_SCANCODE_R:
            activate_single_stepping_on_condition = 1;
            break;

        case SDL_SCANCODE_T:
            activate_single_stepping_on_condition = 0;
            break;
#endif

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

        default:
            break;
    }
}

int gui_main(int argc, char **argv)
{
    _Bool quit = 0;

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

    SDL_CreateWindowAndRenderer(GB_FRAMEBUFFER_WIDTH * SCREEN_SCALE, GB_FRAMEBUFFER_HEIGHT * SCREEN_SCALE, 0, &window, &renderer);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_SetWindowTitle(window, "[ nsGBE ]");

    SDL_CreateThread(system_run_event_loop, "nsgbe_core", NULL);

    display_notify_vblank = &handle_vblank;

    while (!quit)
    {
        SDL_Event e;

        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                quit = 1;

            if (e.type == SDL_KEYDOWN)
                handleKeyDown(e.key);

            if (e.type == SDL_KEYUP)
                handleKeyUp(e.key);
        }

        vblank();

        sprintf(title_buffer, WINDOW_TITLE_FORMATTER, last_framecounter);
        SDL_SetWindowTitle(window, title_buffer);
    }

    write_battery();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
