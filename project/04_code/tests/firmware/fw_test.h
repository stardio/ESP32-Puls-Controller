/* Minimal host test harness (no framework): CHECK macros count failures and print each one. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int g_checks, g_fails;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); g_checks++; if (_a != _b) { g_fails++; \
    printf("  FAIL %s:%d: %s == %s  (got %lld, want %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_RANGE(a, lo, hi) do { long long _a = (long long)(a); g_checks++; if (_a < (long long)(lo) || _a > (long long)(hi)) { g_fails++; \
    printf("  FAIL %s:%d: %s in [%s, %s]  (got %lld)\n", __FILE__, __LINE__, #a, #lo, #hi, _a); } } while (0)
#define RUN(fn) do { int _f = g_fails; fn(); printf("%s %s\n", g_fails == _f ? "PASS" : "FAIL", #fn); } while (0)
#define SUMMARY() do { printf("%s: %d checks, %d failures\n", __FILE__, g_checks, g_fails); return g_fails ? 1 : 0; } while (0)
