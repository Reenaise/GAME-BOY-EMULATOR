/*
 * Entry point: command line, SDL event loop, frame pacing.
 *
 * Controls follow Cinoop's Windows build (Z = B, X = A, Enter = Start,
 * Backspace = Select, arrow keys, Space = debugger, Esc = quit) and can be
 * remapped with a key file (--keys, see README).
 */

#include <SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartridge.h"
#include "cpu.h"
#include "debugger.h"
#include "display.h"
#include "emulator.h"
#include "input.h"
#include "ppu.h"
#include "screenshot.h"
#include "serial.h"

struct options {
    const char *romPath;
    const char *bootRomPath;
    const char *keysPath;
    const char *tracePath;
    const char *screenshotPath;
    int scale;
    bool debug;
    bool headless;
    bool serial;
    bool info;
    long frames;
};

static SDL_Scancode keymap[BUTTON_COUNT] = {
    SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_X,          /* A      */
    SDL_SCANCODE_Z,          /* B      */
    SDL_SCANCODE_BACKSPACE,  /* Select */
    SDL_SCANCODE_RETURN,     /* Start  */
};

static void printUsage(FILE *out) {
    fprintf(out,
        "Usage: gameboy-emulator [options] <rom.gb>\n"
        "\n"
        "A Game Boy (DMG) emulator based on the Cinoop design. Sound is not emulated.\n"
        "\n"
        "Options:\n"
        "  -h, --help             show this help and exit\n"
        "  -d, --debug            start paused in the console debugger\n"
        "  -s, --scale N          window scale factor 1-10 (default 4)\n"
        "  -k, --keys FILE        load a key mapping file\n"
        "      --bootrom FILE     run a 256 byte DMG boot ROM first\n"
        "      --trace FILE       write an instruction trace to FILE ('-' = stdout)\n"
        "      --serial           print bytes sent over the serial port to stdout\n"
        "      --info             print the cartridge header and exit\n"
        "      --headless         run without a window (for testing)\n"
        "      --frames N         quit after N frames (headless default: 600)\n"
        "      --screenshot FILE  save the last frame as PNG when quitting\n"
        "\n"
        "Controls (default):\n"
        "  Arrow keys  D-pad        X  A        Z  B\n"
        "  Enter       Start        Backspace  Select\n"
        "  P  pause    Tab (hold)  fast forward    F2  reset\n"
        "  F12 screenshot          Space / F1  break into debugger    Esc  quit\n");
}

static bool parseInt(const char *text, long min, long max, long *out) {
    char *end;
    long value = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value < min || value > max) return false;
    *out = value;
    return true;
}

/* Returns 0 = run, 1 = exit success (help), 2 = usage error. */
static int parseArguments(int argc, char **argv, struct options *options) {
    int i;
    memset(options, 0, sizeof(*options));
    options->scale = 4;
    options->frames = 0; /* 0 = unlimited (windowed) / 600 (headless) */

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        bool hasValue = i + 1 < argc;
        long value;

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            printUsage(stdout);
            return 1;
        } else if (!strcmp(arg, "-d") || !strcmp(arg, "--debug")) {
            options->debug = true;
        } else if (!strcmp(arg, "--headless")) {
            options->headless = true;
        } else if (!strcmp(arg, "--serial")) {
            options->serial = true;
        } else if (!strcmp(arg, "--info")) {
            options->info = true;
        } else if (!strcmp(arg, "-s") || !strcmp(arg, "--scale")) {
            if (!hasValue || !parseInt(argv[++i], 1, 10, &value)) {
                fprintf(stderr, "Error: --scale needs a number between 1 and 10\n");
                return 2;
            }
            options->scale = (int)value;
        } else if (!strcmp(arg, "--frames")) {
            if (!hasValue || !parseInt(argv[++i], 1, 100000000L, &value)) {
                fprintf(stderr, "Error: --frames needs a positive number\n");
                return 2;
            }
            options->frames = value;
        } else if (!strcmp(arg, "-k") || !strcmp(arg, "--keys") || !strcmp(arg, "--bootrom") ||
                   !strcmp(arg, "--trace") || !strcmp(arg, "--screenshot")) {
            if (!hasValue) {
                fprintf(stderr, "Error: %s needs a file name\n", arg);
                return 2;
            }
            if (!strcmp(arg, "--bootrom")) options->bootRomPath = argv[++i];
            else if (!strcmp(arg, "--trace")) options->tracePath = argv[++i];
            else if (!strcmp(arg, "--screenshot")) options->screenshotPath = argv[++i];
            else options->keysPath = argv[++i];
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "Error: unknown option '%s' (try --help)\n", arg);
            return 2;
        } else if (options->romPath) {
            fprintf(stderr, "Error: more than one ROM given ('%s' and '%s')\n", options->romPath, arg);
            return 2;
        } else {
            options->romPath = arg;
        }
    }

    if (!options->romPath) {
        fprintf(stderr, "Error: no ROM file given.\n\n");
        printUsage(stderr);
        return 2;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Key mapping file:  "<Button> = <SDL key name>" per line, # comments.       */
/* Example:  A = X    Start = Return    Up = W                                */
/* ------------------------------------------------------------------------- */

static char *trim(char *s) {
    char *end;
    while (isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool loadKeymap(const char *path) {
    FILE *file = fopen(path, "r");
    char line[256];
    int lineNumber = 0;

    if (!file) {
        fprintf(stderr, "Error: cannot open key file '%s'\n", path);
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        char *name, *key, *equals;
        int b;
        SDL_Scancode scancode;

        lineNumber++;
        if ((equals = strchr(line, '#')) != NULL) *equals = '\0';
        name = trim(line);
        if (!*name) continue;
        equals = strchr(name, '=');
        if (!equals) {
            fprintf(stderr, "%s:%d: expected '<Button> = <Key>'\n", path, lineNumber);
            continue;
        }
        *equals = '\0';
        name = trim(name);
        key = trim(equals + 1);

        for (b = 0; b < BUTTON_COUNT; b++) {
            if (SDL_strcasecmp(name, buttonName((enum button)b)) == 0) break;
        }
        if (b == BUTTON_COUNT) {
            fprintf(stderr, "%s:%d: unknown button '%s'\n", path, lineNumber, name);
            continue;
        }
        scancode = SDL_GetScancodeFromName(key);
        if (scancode == SDL_SCANCODE_UNKNOWN) {
            fprintf(stderr, "%s:%d: unknown key '%s'\n", path, lineNumber, key);
            continue;
        }
        keymap[b] = scancode;
    }
    fclose(file);
    return true;
}

/* ------------------------------------------------------------------------- */

static int runHeadless(const struct options *options) {
    long frame;
    long frames = options->frames ? options->frames : 600;
    for (frame = 0; frame < frames; frame++) {
        if (emulatorRunFrame() == RUN_BREAKPOINT) {
            if (!debuggerPrompt()) break;
        }
    }
    if (cpu.locked) {
        fprintf(stderr, "CPU hung on illegal opcode 0x%02X at 0x%04X\n", cpu.lockedOpcode, registers.pc);
    }
    if (options->screenshotPath) {
        if (!screenshotSavePng(options->screenshotPath, &ppuFrontBuffer[0][0])) {
            fprintf(stderr, "Error: could not write screenshot '%s'\n", options->screenshotPath);
            return 1;
        }
        printf("Saved screenshot to %s\n", options->screenshotPath);
    }
    printf("Ran %ld frames, %llu CPU cycles, PC=0x%04X\n", frame,
           (unsigned long long)cpu.ticks, registers.pc);
    return 0;
}

static void handleKey(SDL_Scancode scancode, bool pressed) {
    int b;
    for (b = 0; b < BUTTON_COUNT; b++) {
        if (keymap[b] == scancode) inputSetButton((enum button)b, pressed);
    }
}

static int runWindowed(const struct options *options) {
    char title[128];
    bool running = true;
    bool paused = false;
    bool fastForward = false;
    bool reportedLock = false;
    int screenshotCount = 0;
    Uint64 frequency, nextFrame, fpsTimer;
    int fpsFrames = 0;
    long totalFrames = 0;
    const double frameTime = 1.0 / GB_FRAME_RATE;

    snprintf(title, sizeof(title), "Game Boy - %s",
             cartridge.header.title[0] ? cartridge.header.title : "untitled");

    if (!displayInit(options->scale, title)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Game Boy emulator",
                                 "Could not initialise SDL2 graphics. See the console for details.", NULL);
        return 1;
    }

    frequency = SDL_GetPerformanceFrequency();
    nextFrame = SDL_GetPerformanceCounter();
    fpsTimer = nextFrame;

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                bool down = event.type == SDL_KEYDOWN;
                SDL_Scancode sc = event.key.keysym.scancode;

                handleKey(sc, down);
                if (!down) {
                    if (sc == SDL_SCANCODE_TAB) fastForward = false;
                    continue;
                }
                if (event.key.repeat) continue;

                switch (sc) {
                    case SDL_SCANCODE_ESCAPE: running = false; break;
                    case SDL_SCANCODE_P: paused = !paused; break;
                    case SDL_SCANCODE_TAB: fastForward = true; break;
                    case SDL_SCANCODE_F2:
                        emulatorReset();
                        reportedLock = false;
                        printf("Reset\n");
                        break;
                    case SDL_SCANCODE_SPACE:
                    case SDL_SCANCODE_F1:
                        printf("Debugger: switch to this console window.\n");
                        debugger.paused = true;
                        debugger.enabled = true;
                        break;
                    case SDL_SCANCODE_F12: {
                        char name[64];
                        snprintf(name, sizeof(name), "screenshot%03d.png", screenshotCount++);
                        if (screenshotSavePng(name, &ppuFrontBuffer[0][0])) printf("Saved %s\n", name);
                        break;
                    }
                    default: break;
                }
            }
        }
        if (!running) break;

        if (debugger.paused) {
            displayPresent(&ppuFrontBuffer[0][0]);
            if (!debuggerPrompt()) break;
            nextFrame = SDL_GetPerformanceCounter();
            continue;
        }

        if (paused) {
            displayPresent(&ppuFrontBuffer[0][0]);
            SDL_Delay(16);
            nextFrame = SDL_GetPerformanceCounter();
            continue;
        }

        emulatorRunFrame();

        if (cpu.locked && !reportedLock) {
            fprintf(stderr, "The CPU hung on illegal opcode 0x%02X at 0x%04X (as real hardware would).\n",
                    cpu.lockedOpcode, registers.pc);
            reportedLock = true;
        }

        displayPresent(&ppuFrontBuffer[0][0]);
        fpsFrames++;
        totalFrames++;
        if (options->frames && totalFrames >= options->frames) running = false;

        /* Frame pacing: ~59.73 frames per second unless fast-forwarding. */
        nextFrame += (Uint64)(frameTime * (double)frequency);
        if (!fastForward) {
            Uint64 now = SDL_GetPerformanceCounter();
            if (now < nextFrame) {
                Uint64 waitMs = (nextFrame - now) * 1000 / frequency;
                if (waitMs > 1) SDL_Delay((Uint32)(waitMs - 1));
                while (SDL_GetPerformanceCounter() < nextFrame) { /* fine wait */ }
            } else if (now - nextFrame > frequency / 4) {
                nextFrame = now; /* fell far behind (e.g. window dragged): resync */
            }
        } else {
            nextFrame = SDL_GetPerformanceCounter();
        }

        if (SDL_GetPerformanceCounter() - fpsTimer >= frequency) {
            char text[160];
            snprintf(text, sizeof(text), "%s - %d fps%s", title, fpsFrames, fastForward ? " (fast)" : "");
            displaySetTitle(text);
            fpsFrames = 0;
            fpsTimer = SDL_GetPerformanceCounter();
        }
    }

    if (options->screenshotPath) {
        if (screenshotSavePng(options->screenshotPath, &ppuFrontBuffer[0][0]))
            printf("Saved screenshot to %s\n", options->screenshotPath);
        else
            fprintf(stderr, "Error: could not write screenshot '%s'\n", options->screenshotPath);
    }
    printf("Ran %ld frames in a %dx%d window\n", totalFrames, SCREEN_WIDTH * options->scale,
           SCREEN_HEIGHT * options->scale);
    displayShutdown();
    return 0;
}

int main(int argc, char **argv) {
    struct options options;
    cartError error;
    int result;

    result = parseArguments(argc, argv, &options);
    if (result == 1) return 0;
    if (result == 2) {
        if (argc < 2) {
            /* Probably started by double-clicking: tell the user in a dialog. */
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Game Boy emulator",
                "Usage: gameboy-emulator [options] <rom.gb>\n\n"
                "Run it from a terminal, or drag a .gb file onto the executable.", NULL);
        }
        return 2;
    }

    error = emulatorLoad(options.romPath, options.bootRomPath);
    if (error != CART_OK) {
        fprintf(stderr, "Error: cannot load '%s': %s\n", options.romPath, cartridgeErrorString(error));
        if (error == CART_ERR_UNSUPPORTED) {
            fprintf(stderr, "       The cartridge type is 0x%02X (%s).\n",
                    cartridge.header.type, cartridgeTypeName(cartridge.header.type));
        }
        return 1;
    }

    if (options.info) {
        cartridgePrintInfo(stdout);
        emulatorShutdown();
        return 0;
    }

    printf("Loaded '%s': %s, %u KiB ROM, %u KiB RAM\n", cartridge.header.title,
           cartridgeTypeName(cartridge.header.type), (unsigned)(cartridge.romSize / 1024),
           (unsigned)(cartridge.ramSize / 1024));
    if (!cartridge.header.logoValid) printf("Warning: Nintendo logo mismatch (a real DMG would refuse to boot this)\n");
    if (!cartridge.header.headerChecksumValid) printf("Warning: header checksum mismatch\n");
    if (cartridge.header.cgbFlag == 0xC0) printf("Warning: this is a Game Boy Color only game\n");

    if (options.keysPath && !loadKeymap(options.keysPath)) {
        emulatorShutdown();
        return 1;
    }

    if (options.serial) serialSetOutput(stdout);

    if (options.tracePath) {
        debugger.trace = strcmp(options.tracePath, "-") ? fopen(options.tracePath, "w") : stdout;
        if (!debugger.trace) {
            fprintf(stderr, "Error: cannot open trace file '%s'\n", options.tracePath);
            emulatorShutdown();
            return 1;
        }
    }

    if (options.debug) {
        debugger.enabled = true;
        debugger.paused = true;
        printf("Debugger active. Type 'h' for help, 'c' to run.\n");
    }

    result = options.headless ? runHeadless(&options) : runWindowed(&options);

    if (debugger.trace && debugger.trace != stdout) fclose(debugger.trace);
    if (cartridge.hasBattery) {
        if (cartridgeSaveRam()) printf("Saved cartridge RAM to %s\n", cartridge.savePath);
    }
    cartridgeUnload();
    return result;
}
