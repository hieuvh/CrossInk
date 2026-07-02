#include <cassert>
#include <cstdio>
#include <cstring>

#include "src/services/TimeFormat.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_STREQ(a, b)                                                              \
  do {                                                                                  \
    const char* _a = (a); const char* _b = (b);                                         \
    if (strcmp(_a, _b) != 0) {                                                          \
      fprintf(stderr, "  FAIL: %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a, _b); \
      testsFailed++; return;                                                            \
    }                                                                                   \
  } while (0)

#define ASSERT_TRUE(c)                                                   \
  do {                                                                   \
    if (!(c)) {                                                          \
      fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c);    \
      testsFailed++; return;                                             \
    }                                                                    \
  } while (0)

#define PASS() testsPassed++

static void test_24h_basic() {
  char buf[16];
  ASSERT_TRUE(TimeFormat::format(1735689600 + 14*3600 + 32*60, /*offset*/0, /*format24h*/true, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "14:32");
  PASS();
}

static void test_24h_midnight() {
  char buf[16];
  ASSERT_TRUE(TimeFormat::format(1735689600, 0, true, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "00:00");
  PASS();
}

static void test_24h_with_negative_offset_wraps() {
  char buf[16];
  ASSERT_TRUE(TimeFormat::format(1735689600, -7, true, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "17:00");
  PASS();
}

static void test_12h_render() {
  char buf[16];
  ASSERT_TRUE(TimeFormat::format(1735689600, 0, false, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "12:00 AM");
  ASSERT_TRUE(TimeFormat::format(1735689600 + 12*3600, 0, false, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "12:00 PM");
  ASSERT_TRUE(TimeFormat::format(1735689600 + 13*3600 + 5*60, 0, false, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "1:05 PM");
  ASSERT_TRUE(TimeFormat::format(1735689600 + 23*3600 + 59*60, 0, false, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "11:59 PM");
  PASS();
}

static void test_sanity_floor_rejects_pre_2025() {
  char buf[16];
  ASSERT_TRUE(!TimeFormat::format(1735689600 - 1, 0, true, buf, sizeof(buf)));
  PASS();
}

static void test_sanity_ceiling_rejects_post_2100() {
  char buf[16];
  ASSERT_TRUE(!TimeFormat::format(int64_t(4102444800) + 1, 0, true, buf, sizeof(buf)));
  PASS();
}

static void test_buffer_too_small_returns_false() {
  char buf[4];
  ASSERT_TRUE(!TimeFormat::format(1735689600, 0, true, buf, sizeof(buf)));
  PASS();
}

static void test_max_positive_offset() {
  char buf[16];
  ASSERT_TRUE(TimeFormat::format(1735689600, 14, true, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "14:00");
  PASS();
}

static void test_max_negative_offset() {
  char buf[16];
  ASSERT_TRUE(TimeFormat::format(1735689600 + 12*3600, -12, true, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "00:00");
  PASS();
}

int main() {
  test_24h_basic();
  test_24h_midnight();
  test_24h_with_negative_offset_wraps();
  test_12h_render();
  test_sanity_floor_rejects_pre_2025();
  test_sanity_ceiling_rejects_post_2100();
  test_buffer_too_small_returns_false();
  test_max_positive_offset();
  test_max_negative_offset();
  fprintf(stderr, "Passed: %d, Failed: %d\n", testsPassed, testsFailed);
  return testsFailed == 0 ? 0 : 1;
}
