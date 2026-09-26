// Tiny test framework: CHECK records failures, each test file's main() returns the count.
#pragma once
#include <stdio.h>
static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST(name) static void name()
#define RUN(name) do { printf("- %s\n", #name); name(); } while (0)
#define DONE() do { printf("%s: %d passed, %d failed\n", __FILE__, g_pass, g_fail); \
    return g_fail ? 1 : 0; } while (0)
