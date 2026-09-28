#ifndef GB_DISPLAY_H
#define GB_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "ppu.h"

/* SDL2 front end: window, texture upload, event handling. */

bool displayInit(int scale, const char *title);
void displayShutdown(void);
/* Upload the 160x144 shade buffer to the texture and present it scaled. */
void displayPresent(const uint8_t *framebuffer);
void displaySetTitle(const char *title);

#endif
