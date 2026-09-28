#include <string.h>

#include "test.h"

int testChecks;
int testFailures;
const char *currentTest = "";

void runTests(const char *suite, const struct testCase *tests, size_t count) {
    size_t i;
    printf("== %s ==\n", suite);
    for (i = 0; i < count; i++) {
        int failuresBefore = testFailures;
        currentTest = tests[i].name;
        tests[i].function();
        printf("  %-40s %s\n", tests[i].name, testFailures == failuresBefore ? "ok" : "FAILED");
    }
}

static const struct {
    const char *name;
    void (*run)(void);
} suites[] = {
    { "cpu", suiteCpu },
    { "memory", suiteMemory },
    { "cartridge", suiteCartridge },
    { "interrupts", suiteInterrupts },
    { "timer", suiteTimer },
    { "ppu", suitePpu },
    { "input", suiteInput },
    { "integration", suiteIntegration },
};

int main(int argc, char **argv) {
    size_t i;
    bool ran = false;

    for (i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (argc < 2 || strcmp(argv[1], suites[i].name) == 0) {
            suites[i].run();
            ran = true;
        }
    }
    if (!ran) {
        printf("Unknown suite '%s'\n", argv[1]);
        return 2;
    }

    printf("\n%d checks, %d failures\n", testChecks, testFailures);
    return testFailures ? 1 : 0;
}
