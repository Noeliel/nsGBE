// SPDX-FileCopyrightText: 2021 Noeliel <noelieldev@gmail.com>
//
// SPDX-License-Identifier: LGPL-2.0-only

#include <sys/time.h>
#include <gtk/gtk.h>
#include "../../emu/nsgbe.h"

#define WINDOW_TITLE_FORMATTER "[ nsGBE ] [ %d fps ]"

#define SCREEN_SCALE 3

#define KEY_SPACE 0x20 // speed up
#define KEY_K     0x6B // A
#define KEY_O     0x6F // B
#define KEY_L     0x6C // start
#define KEY_P     0x70 // select
#define KEY_W     0x77 // up
#define KEY_S     0x73 // down
#define KEY_A     0x61 // left
#define KEY_D     0x64 // right

uint32_t *framebuffer;

char title_buffer[32];
uint16_t last_framecounter = 0;

GtkWidget *window;
GtkWidget *display;
static cairo_surface_t *surface;

union BUTTON_STATE button_states;

static void close_window()
{
    write_battery();

    if (surface)
        cairo_surface_destroy(surface);
}

static void clear_surface()
{
    cairo_t *cr;

    cr = cairo_create(surface);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    cairo_destroy(cr);
}

void vblank()
{
    framebuffer = display_request_next_frame();

    cairo_t *cr;

    clear_surface();

    cr = cairo_create(surface);

    for (int y = 0; y < GB_FRAMEBUFFER_HEIGHT; y++)
    {
        for (int x = 0; x < GB_FRAMEBUFFER_WIDTH; x++)
        {
            uint32_t color = *(framebuffer + (y * GB_FRAMEBUFFER_WIDTH + x));
            uint8_t color_b = (color >> 16) & 0xFF;
            uint8_t color_g = (color >> 8) & 0xFF;
            uint8_t color_r = color & 0xFF;

            cairo_set_source_rgb(cr, (float)color_r / 255.f, (float)color_g / 255.f, (float)color_b / 255.f);
            cairo_rectangle(cr, x * SCREEN_SCALE, y * SCREEN_SCALE, SCREEN_SCALE, SCREEN_SCALE);
            cairo_fill(cr);
        }
    }

    cairo_destroy(cr);
}

static gboolean setup_draw_surface(GtkWidget *widget, GdkEventConfigure *event, gpointer data)
{
    if (surface)
        cairo_surface_destroy(surface);

    surface = gdk_window_create_similar_surface(gtk_widget_get_window(widget), CAIRO_CONTENT_COLOR, gtk_widget_get_allocated_width(widget), \
      gtk_widget_get_allocated_height(widget));

    clear_surface();

    return TRUE;
}

static gboolean redraw_display(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    vblank();

    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);

    sprintf(title_buffer, WINDOW_TITLE_FORMATTER, last_framecounter);
    gtk_window_set_title(GTK_WINDOW(window), title_buffer);

    return FALSE;
}

/*
static void draw_hi()
{
    // H

    framebuffer[4 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;
    framebuffer[5 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;
    framebuffer[6 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;
    framebuffer[7 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;
    framebuffer[8 * GB_FRAMEBUFFER_WIDTH + 4] = 0xFF;

    framebuffer[4 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;
    framebuffer[5 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;
    framebuffer[6 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;
    framebuffer[7 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;
    framebuffer[8 * GB_FRAMEBUFFER_WIDTH + 5] = 0xFF;

    framebuffer[6 * GB_FRAMEBUFFER_WIDTH + 6] = 0xFF;
    framebuffer[6 * GB_FRAMEBUFFER_WIDTH + 7] = 0xFF;

    framebuffer[4 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;
    framebuffer[5 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;
    framebuffer[6 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;
    framebuffer[7 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;
    framebuffer[8 * GB_FRAMEBUFFER_WIDTH + 8] = 0xFF;

    framebuffer[4 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;
    framebuffer[5 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;
    framebuffer[6 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;
    framebuffer[7 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;
    framebuffer[8 * GB_FRAMEBUFFER_WIDTH + 9] = 0xFF;

    // I

    framebuffer[4 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;
    framebuffer[5 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;
    framebuffer[6 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;
    framebuffer[7 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;
    framebuffer[8 * GB_FRAMEBUFFER_WIDTH + 12] = 0xFF;

    framebuffer[4 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;
    framebuffer[5 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;
    framebuffer[6 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;
    framebuffer[7 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;
    framebuffer[8 * GB_FRAMEBUFFER_WIDTH + 13] = 0xFF;

    // !

    framebuffer[4 * GB_FRAMEBUFFER_WIDTH + 17] = 0xFF;
    framebuffer[5 * GB_FRAMEBUFFER_WIDTH + 17] = 0xFF;
    framebuffer[8 * GB_FRAMEBUFFER_WIDTH + 17] = 0xFF;

    framebuffer[4 * GB_FRAMEBUFFER_WIDTH + 18] = 0xFF;
    framebuffer[5 * GB_FRAMEBUFFER_WIDTH + 18] = 0xFF;
    framebuffer[6 * GB_FRAMEBUFFER_WIDTH + 18] = 0xFF;
    framebuffer[8 * GB_FRAMEBUFFER_WIDTH + 18] = 0xFF;
}
*/

static gint handle_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    //if (event->length > 0)
        //printf("pressed (string) 0x%02X\n", event->keyval);

    switch (event->keyval)
    {
        case KEY_SPACE:
            system_overclock = 1;
            break;

        case KEY_K:
            button_states.A = 1;
            break;

        case KEY_O:
            button_states.B = 1;
            break;

        case KEY_L:
            button_states.START = 1;
            break;

        case KEY_P:
            button_states.SELECT = 1;
            break;

        case KEY_W:
            button_states.UP = 1;
            break;

        case KEY_S:
            button_states.DOWN = 1;
            break;

        case KEY_A:
            button_states.LEFT = 1;
            break;

        case KEY_D:
            button_states.RIGHT = 1;
            break;

        default:
            break;
    }
}

static gint handle_key_release(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    //if (event->length > 0)
        //printf("released (string) 0x%02X\n", event->keyval);

    switch (event->keyval)
    {
        case KEY_SPACE:
            system_overclock = 0;
            break;

        case KEY_K:
            button_states.A = 0;
            break;

        case KEY_O:
            button_states.B = 0;
            break;

        case KEY_L:
            button_states.START = 0;
            break;

        case KEY_P:
            button_states.SELECT = 0;
            break;

        case KEY_W:
            button_states.UP = 0;
            break;

        case KEY_S:
            button_states.DOWN = 0;
            break;

        case KEY_A:
            button_states.LEFT = 0;
            break;

        case KEY_D:
            button_states.RIGHT = 0;
            break;

        default:
            break;
    }
}

static void activate(GtkApplication* app, gpointer user_data)
{
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "[ nsGBE ]");
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    //gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), GB_FRAMEBUFFER_WIDTH * SCREEN_SCALE, GB_FRAMEBUFFER_HEIGHT * SCREEN_SCALE);
    gtk_container_set_border_width((GtkContainer *)GTK_WINDOW(window), 0);

    g_signal_connect(window, "key_press_event", G_CALLBACK(handle_key_press), NULL);
    g_signal_connect(window, "key_release_event", G_CALLBACK(handle_key_release), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(close_window), NULL);

    display = gtk_drawing_area_new();
    gtk_widget_set_size_request(display, GB_FRAMEBUFFER_WIDTH * SCREEN_SCALE, GB_FRAMEBUFFER_HEIGHT * SCREEN_SCALE);

    g_signal_connect(display, "configure-event", G_CALLBACK(setup_draw_surface), NULL);
    g_signal_connect(display, "draw", G_CALLBACK(redraw_display), NULL);

    gtk_container_add(GTK_CONTAINER(window), display);

    gtk_widget_show_all(window);
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

    if (display)
        gtk_widget_queue_draw(display);
}

int gui_main(int argc, char **argv)
{
    GtkApplication *app;
    int status;

    display_notify_vblank = &handle_vblank;

    app = gtk_application_new("com.noeliel.nsgbe", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
