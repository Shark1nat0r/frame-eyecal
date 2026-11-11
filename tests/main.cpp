// Unit tests for the parts that run without SteamVR. Build and run: tests/run.sh
#include <cstdio>

#include "check.h"

int main() {
    for (const auto &t : test_cases()) {
        int before = test_failures();
        t.fn();
        if (test_failures() != before) fprintf(stderr, "FAILED: %s\n", t.name);
    }
    printf("%zu tests, %d failed checks\n", test_cases().size(), test_failures());
    return test_failures() ? 1 : 0;
}
