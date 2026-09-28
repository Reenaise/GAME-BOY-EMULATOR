#ifndef GB_TEST_H
#define GB_TEST_H

/*
 * A tiny test framework: no dependencies, one executable, CTest runs each
 * suite separately (gb_tests <suite>).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern int testChecks;
extern int testFailures;
extern const char *currentTest;

#define CHECK(cond) do { \
    testChecks++; \
    if (!(cond)) { \
        testFailures++; \
        printf("    FAIL [%s] %s:%d: %s\n", currentTest, __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    long long checkActual_ = (long long)(actual); \
    long long checkExpected_ = (long long)(expected); \
    testChecks++; \
    if (checkActual_ != checkExpected_) { \
        testFailures++; \
        printf("    FAIL [%s] %s:%d: %s == 0x%llX, expected 0x%llX\n", \
               currentTest, __FILE__, __LINE__, #actual, checkActual_, checkExpected_); \
    } \
} while (0)

struct testCase {
    const char *name;
    void (*function)(void);
};

#define TEST(name) { #name, name }

void runTests(const char *suite, const struct testCase *tests, size_t count);

/* ---- helpers (test_util.c) ------------------------------------------ */

/* Fill `rom` with a valid header (logo, title, checksum) of the given type. */
void testBuildRom(uint8_t *rom, size_t size, uint8_t type, uint8_t romSizeCode, uint8_t ramSizeCode);
/* Load a blank 32 KiB ROM-only cartridge and reset the machine. */
void testResetMachine(void);
/* Copy code to WRAM at 0xC000, set PC there, SP to 0xDFF0, IME off. */
void testLoadCode(const uint8_t *code, size_t length);
/* Execute `count` instructions; returns the total cycles used. */
int testStep(int count);

/* Suites */
void suiteCpu(void);
void suiteMemory(void);
void suiteCartridge(void);
void suiteInterrupts(void);
void suiteTimer(void);
void suitePpu(void);
void suiteInput(void);
void suiteIntegration(void);

#endif
