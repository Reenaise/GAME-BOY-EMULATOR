#ifndef GB_SCREENSHOT_H
#define GB_SCREENSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "ppu.h"

/* RGB colours for the four DMG shades (lightest first), 0xRRGGBB. */
extern uint32_t displayPalette[4];

/* Write the framebuffer as an uncompressed PNG. No external libraries. */
bool screenshotSavePng(const char *path, const uint8_t *framebuffer);

#endif
