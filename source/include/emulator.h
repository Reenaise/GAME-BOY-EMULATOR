#ifndef GB_EMULATOR_H
#define GB_EMULATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "cartridge.h"

#define GB_CLOCK_HZ 4194304.0
#define GB_FRAME_RATE (GB_CLOCK_HZ / 70224.0) /* ~59.73 Hz */

typedef enum {
    RUN_FRAME_COMPLETE,  /* a full frame was produced               */
    RUN_BREAKPOINT,      /* the debugger asked to stop              */
} runResult;

/* Load a ROM (and optional boot ROM) and reset the machine. */
cartError emulatorLoad(const char *romPath, const char *bootRomPath);
/* Reset every component to the post-boot-ROM state. The cartridge stays. */
void emulatorReset(void);
void emulatorShutdown(void);

/* One CPU instruction plus the matching timer/serial/PPU time. */
int emulatorStep(void);
/* Run until the PPU finishes a frame (or ~1 frame of time with LCD off). */
runResult emulatorRunFrame(void);

#endif
