/*
 * SDL2 display (Cinoop: display.c / platform code).
 *
 * The PPU produces a 160x144 buffer of shade indices. Each frame it is
 * converted to ARGB, uploaded to a 160x144 streaming texture and drawn
 * scaled to the window. Scaling only happens here, never in the emulator.
 */

#include "display.h"

#include <SDL.h>
#include <stdio.h>

#include "screenshot.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

bool displayInit(int scale, const char *title) {
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "Error: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    /* Nearest-neighbour scaling keeps the pixels sharp. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              SCREEN_WIDTH * scale, SCREEN_HEIGHT * scale,
                              SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "Error: could not create window: %s\n", SDL_GetError());
        displayShutdown();
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        /* No GPU acceleration available (e.g. remote desktop): fall back. */
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        fprintf(stderr, "Error: could not create renderer: %s\n", SDL_GetError());
        displayShutdown();
        return false;
    }

    /* Keep the 10:9 aspect ratio when the window is resized. */
    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!texture) {
        fprintf(stderr, "Error: could not create texture: %s\n", SDL_GetError());
        displayShutdown();
        return false;
    }

    return true;
}

void displayShutdown(void) {
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    texture = NULL;
    renderer = NULL;
    window = NULL;
    SDL_Quit();
}

void displaySetTitle(const char *title) {
    if (window) SDL_SetWindowTitle(window, title);
}

void displayPresent(const uint8_t *framebuffer) {
    static uint32_t pixels[SCREEN_HEIGHT * SCREEN_WIDTH];
    int x, y;

    for (y = 0; y < SCREEN_HEIGHT; y++) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            pixels[y * SCREEN_WIDTH + x] = 0xFF000000u | displayPalette[framebuffer[y * SCREEN_WIDTH + x] & 3];
        }
    }

    SDL_UpdateTexture(texture, NULL, pixels, SCREEN_WIDTH * (int)sizeof(uint32_t));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}
