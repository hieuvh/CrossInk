# Clock Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the current time on the left of the Home header on both X3 (DS3231) and X4 (internal RTC + cold-boot NTP), with a new Time category in Settings (24h/12h toggle, UTC offset, Sync now, Set time manually) and a small web UI.

**Architecture:** Polymorphic `RtcBackend` (DS3231 or internal) behind a single `TimeService` singleton. NTP code extracted from `KOReaderSyncActivity` into a reusable `NtpSyncService`. Last-synced UTC persisted to SD via an atomically-written `/.crosspoint/time.bin` for X4 cold-boot fallback. Header reads time only on Home redraws (no background refresh).

**Tech Stack:** C++20 / Arduino-ESP32 / ESP-IDF (esp_sntp, FreeRTOS), PlatformIO, SdFat-backed `HalStorage`, Wire (I2C) for DS3231.

**Spec:** [`docs/superpowers/specs/2026-05-15-clock-display-design.md`](../specs/2026-05-15-clock-display-design.md)

---

## File structure

| File | Role |
|---|---|
| `lib/hal/HalRtc.h` (new) | `RtcBackend` interface (`readUtcEpoch`, `writeUtcEpoch`) |
| `lib/hal/HalRtcInternal.h` / `.cpp` (new) | `RtcBackend` over `gettimeofday`/`settimeofday` (X4 + X3 fallback) |
| `lib/hal/HalRtcDS3231.h` / `.cpp` (new) | `RtcBackend` over the DS3231 I2C registers (X3) |
| `src/services/TimeService.h` / `.cpp` (new) | App-layer singleton, owns `RtcBackend` + mutex + `TimeSource` |
| `src/services/NtpSyncService.h` / `.cpp` (new) | Reusable SNTP runner; replaces inline code in `KOReaderSyncActivity` |
| `src/TimePersistenceStore.h` / `.cpp` (new) | SD-backed `time.bin` with CRC + atomic write |
| `src/activities/settings/SetTimeActivity.h` / `.cpp` (new) | On-device HH:MM editor |
| `src/activities/settings/SyncTimeNowActivity.h` / `.cpp` (new) | Modal popup that runs the foreground NTP sync |
| `test/time_format/test_time_format.cpp` (new) | Host-compiled tests for `TimeService::formatLocalAt` |
| `test/time_persistence/test_time_persistence.cpp` (new) | Host-compiled tests for `TimePersistenceStore` |
| `test/run_time_tests.sh` (new) | Build+run both host tests |
| `src/CrossPointSettings.h` (modify) | Add `showHeaderClock`, `timeFormat`, `utcOffsetIndex` |
| `src/SettingsList.h` (modify) | Add Time category + 5 entries (incl. 27 timezone enums) |
| `src/activities/settings/SettingsActivity.h` (modify) | Add `SettingAction::SyncTimeNow`, `SettingAction::SetTimeManual` |
| `src/activities/settings/SettingsActivity.cpp` (modify) | Wire those two action cases |
| `src/components/themes/lyra/LyraTheme.cpp` (modify) | Draw clock when `title==nullptr && showHeaderClock` |
| `src/components/themes/BaseTheme.cpp` (modify) | Same for the classic theme (does not chain into LyraTheme) |
| `src/main.cpp` (modify) | Call `TimeService::instance().boot(deviceType)` once |
| `src/activities/reader/KOReaderSyncActivity.cpp` (modify) | Replace inline `syncTimeWithNTP` with `NtpSyncService::syncOnce` |
| `lib/I18n/translations/english.yaml` (modify) | Add ~14 fixed strings + 27 timezone labels |
| `src/network/html/SettingsPage.html` (modify) | Web UI: device-time field + 2 buttons |
| `src/network/CrossPointWebServer.cpp` (modify) | Register `GET/POST /api/time` and `POST /api/time/sync` |
| `platformio.ini` (modify) | Add `HalRtcDS3231` to simulator `lib_ignore` |
| `CHANGELOG.md` (modify) | `Added` entry per CLAUDE.md |

---

### Task 1: TimePersistenceStore — TDD

**Files:**
- Create: `src/TimePersistenceStore.h`
- Create: `src/TimePersistenceStore.cpp`
- Create: `test/time_persistence/test_time_persistence.cpp`
- Create: `test/run_time_persistence_test.sh`

`TimePersistenceStore` must be host-buildable with no `Arduino.h`/`HalStorage` dependency in its core. Use a tiny `Filesystem` interface that the production code wires to `HalStorage` and the test wires to plain POSIX. This mirrors how the existing `release_json_parser` test isolates pure logic from `HalStorage`.

- [ ] **Step 1: Write `TimePersistenceStore.h`**

```cpp
// src/TimePersistenceStore.h
#pragma once
#include <cstddef>
#include <cstdint>

class TimePersistenceStore {
 public:
  // Filesystem interface — production wires to HalStorage, tests wire to POSIX
  struct Filesystem {
    virtual ~Filesystem() = default;
    virtual bool exists(const char* path) = 0;
    virtual bool remove(const char* path) = 0;
    virtual bool rename(const char* oldPath, const char* newPath) = 0;
    // Read entire file into out[0..outCap); set *outLen. Return false if missing/unreadable.
    virtual bool read(const char* path, uint8_t* out, size_t outCap, size_t* outLen) = 0;
    // Atomically write: write to "<path>.tmp", fsync, rename to <path>.
    virtual bool write(const char* path, const uint8_t* data, size_t len) = 0;
  };

  static constexpr const char* kPath = "/.crosspoint/time.bin";
  static constexpr uint32_t kMagic = 0x54494D45;  // 'TIME'
  static constexpr uint16_t kVersion = 1;
  static constexpr uint32_t kDebounceSeconds = 60;

  explicit TimePersistenceStore(Filesystem& fs) : fs_(fs) {}

  // Load from kPath. On corrupt file (bad magic/version/CRC), deletes the file and returns false.
  bool load();

  // Returns true if a previous load() found a valid lastSyncedUtc.
  bool hasLastSynced() const { return hasLastSynced_; }
  int64_t lastSyncedUtc() const { return lastSyncedUtc_; }

  // Persist new lastSyncedUtc. No-ops (returns true) if |epoch - lastWritten| < kDebounceSeconds.
  bool write(int64_t epoch);

 private:
  Filesystem& fs_;
  bool hasLastSynced_ = false;
  int64_t lastSyncedUtc_ = 0;
  int64_t lastWritten_ = 0;

  static uint32_t crc32(const uint8_t* data, size_t len);
};
```

- [ ] **Step 2: Write the failing test file**

```cpp
// test/time_persistence/test_time_persistence.cpp
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "src/TimePersistenceStore.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define ASSERT_EQ(a, b)                                                           \
  do {                                                                            \
    auto _a = (a); auto _b = (b);                                                 \
    if (_a != _b) {                                                               \
      fprintf(stderr, "  FAIL: %s:%d: %s != expected\n", __FILE__, __LINE__, #a); \
      testsFailed++; return;                                                      \
    }                                                                             \
  } while (0)

#define ASSERT_TRUE(c)                                                   \
  do {                                                                   \
    if (!(c)) {                                                          \
      fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #c);    \
      testsFailed++; return;                                             \
    }                                                                    \
  } while (0)

#define PASS() testsPassed++

// In-memory FS for hermetic tests (no real disk writes)
class MemFs : public TimePersistenceStore::Filesystem {
 public:
  std::map<std::string, std::vector<uint8_t>> files;
  bool failNextWrite = false;
  bool simulateCrashBeforeRename = false;

  bool exists(const char* p) override { return files.count(p) > 0; }
  bool remove(const char* p) override { return files.erase(p) > 0; }
  bool rename(const char* o, const char* n) override {
    auto it = files.find(o);
    if (it == files.end()) return false;
    files[n] = it->second; files.erase(it);
    return true;
  }
  bool read(const char* p, uint8_t* out, size_t cap, size_t* outLen) override {
    auto it = files.find(p);
    if (it == files.end()) return false;
    if (it->second.size() > cap) return false;
    memcpy(out, it->second.data(), it->second.size());
    *outLen = it->second.size();
    return true;
  }
  bool write(const char* p, const uint8_t* data, size_t len) override {
    if (failNextWrite) { failNextWrite = false; return false; }
    std::string tmp = std::string(p) + ".tmp";
    files[tmp] = std::vector<uint8_t>(data, data + len);
    if (simulateCrashBeforeRename) { simulateCrashBeforeRename = false; return false; }
    files[p] = files[tmp];
    files.erase(tmp);
    return true;
  }
};

static void test_round_trip() {
  MemFs fs; TimePersistenceStore s(fs);
  ASSERT_TRUE(s.write(1747000000));
  TimePersistenceStore s2(fs);
  ASSERT_TRUE(s2.load());
  ASSERT_TRUE(s2.hasLastSynced());
  ASSERT_EQ(s2.lastSyncedUtc(), int64_t(1747000000));
  PASS();
}

static void test_load_missing_file() {
  MemFs fs; TimePersistenceStore s(fs);
  ASSERT_TRUE(!s.load());
  ASSERT_TRUE(!s.hasLastSynced());
  PASS();
}

static void test_corrupt_magic_is_rejected_and_deleted() {
  MemFs fs;
  std::vector<uint8_t> garbage(20, 0xAB);
  fs.files[TimePersistenceStore::kPath] = garbage;
  TimePersistenceStore s(fs);
  ASSERT_TRUE(!s.load());
  ASSERT_TRUE(!fs.exists(TimePersistenceStore::kPath));  // deleted
  PASS();
}

static void test_corrupt_crc_is_rejected() {
  MemFs fs; TimePersistenceStore s(fs);
  ASSERT_TRUE(s.write(1747000000));
  // Flip a byte in the payload's middle (not the magic, not the CRC)
  fs.files[TimePersistenceStore::kPath][8] ^= 0x01;
  TimePersistenceStore s2(fs);
  ASSERT_TRUE(!s2.load());
  PASS();
}

static void test_truncated_file_is_rejected() {
  MemFs fs; TimePersistenceStore s(fs);
  ASSERT_TRUE(s.write(1747000000));
  fs.files[TimePersistenceStore::kPath].resize(8);
  TimePersistenceStore s2(fs);
  ASSERT_TRUE(!s2.load());
  PASS();
}

static void test_atomic_write_keeps_old_file_on_crash() {
  MemFs fs; TimePersistenceStore s(fs);
  ASSERT_TRUE(s.write(1747000000));
  auto firstPayload = fs.files[TimePersistenceStore::kPath];
  fs.simulateCrashBeforeRename = true;
  ASSERT_TRUE(!s.write(1747100000));   // crashed mid-write
  // Original file still intact
  ASSERT_EQ(fs.files[TimePersistenceStore::kPath], firstPayload);
  PASS();
}

static void test_debounce_within_60s_is_noop() {
  MemFs fs; TimePersistenceStore s(fs);
  ASSERT_TRUE(s.write(1747000000));
  auto first = fs.files[TimePersistenceStore::kPath];
  ASSERT_TRUE(s.write(1747000030));  // 30s later
  ASSERT_EQ(fs.files[TimePersistenceStore::kPath], first);  // unchanged
  ASSERT_TRUE(s.write(1747000061));  // 61s later
  ASSERT_TRUE(fs.files[TimePersistenceStore::kPath] != first);  // updated
  PASS();
}

int main() {
  test_round_trip();
  test_load_missing_file();
  test_corrupt_magic_is_rejected_and_deleted();
  test_corrupt_crc_is_rejected();
  test_truncated_file_is_rejected();
  test_atomic_write_keeps_old_file_on_crash();
  test_debounce_within_60s_is_noop();
  fprintf(stderr, "Passed: %d, Failed: %d\n", testsPassed, testsFailed);
  return testsFailed == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Write the build/run script**

```bash
#!/usr/bin/env bash
# test/run_time_persistence_test.sh
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/time_persistence"
BINARY="$BUILD_DIR/TimePersistenceTest"
mkdir -p "$BUILD_DIR"
SOURCES=(
  "$ROOT_DIR/test/time_persistence/test_time_persistence.cpp"
  "$ROOT_DIR/src/TimePersistenceStore.cpp"
)
CXXFLAGS=(-std=c++20 -O2 -Wall -Wextra -pedantic -I"$ROOT_DIR")
c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"
"$BINARY" "$@"
```

```bash
chmod +x test/run_time_persistence_test.sh
```

- [ ] **Step 4: Run the script and confirm it fails to compile**

Run: `bash test/run_time_persistence_test.sh`
Expected: link/compile error — `TimePersistenceStore.cpp` does not exist yet.

- [ ] **Step 5: Implement `TimePersistenceStore.cpp`**

```cpp
// src/TimePersistenceStore.cpp
#include "TimePersistenceStore.h"
#include <cstring>

namespace {
struct Payload {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  int64_t  lastSyncedUtc;
  uint32_t crc32;
};
constexpr size_t kPayloadCoreSize = offsetof(Payload, crc32);  // bytes covered by CRC
constexpr size_t kPayloadTotalSize = sizeof(Payload);
}

uint32_t TimePersistenceStore::crc32(const uint8_t* data, size_t len) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    c ^= data[i];
    for (int b = 0; b < 8; ++b) {
      c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
  }
  return ~c;
}

bool TimePersistenceStore::load() {
  hasLastSynced_ = false;
  if (!fs_.exists(kPath)) return false;

  uint8_t buf[kPayloadTotalSize];
  size_t n = 0;
  if (!fs_.read(kPath, buf, sizeof(buf), &n) || n != sizeof(buf)) {
    fs_.remove(kPath);
    return false;
  }

  Payload p;
  memcpy(&p, buf, sizeof(p));
  if (p.magic != kMagic || p.version != kVersion) {
    fs_.remove(kPath);
    return false;
  }
  uint32_t expected = crc32(buf, kPayloadCoreSize);
  if (p.crc32 != expected) {
    fs_.remove(kPath);
    return false;
  }
  lastSyncedUtc_ = p.lastSyncedUtc;
  lastWritten_   = p.lastSyncedUtc;
  hasLastSynced_ = true;
  return true;
}

bool TimePersistenceStore::write(int64_t epoch) {
  if (hasLastSynced_) {
    int64_t delta = epoch > lastWritten_ ? epoch - lastWritten_ : lastWritten_ - epoch;
    if (delta < int64_t(kDebounceSeconds)) return true;  // debounced no-op
  }
  Payload p{};
  p.magic = kMagic;
  p.version = kVersion;
  p.reserved = 0;
  p.lastSyncedUtc = epoch;
  uint8_t buf[kPayloadTotalSize];
  memcpy(buf, &p, sizeof(p));
  uint32_t c = crc32(buf, kPayloadCoreSize);
  memcpy(buf + kPayloadCoreSize, &c, sizeof(c));

  if (!fs_.write(kPath, buf, sizeof(buf))) return false;

  lastWritten_   = epoch;
  lastSyncedUtc_ = epoch;
  hasLastSynced_ = true;
  return true;
}
```

- [ ] **Step 6: Run tests and verify all pass**

Run: `bash test/run_time_persistence_test.sh`
Expected output: `Passed: 7, Failed: 0` and exit code 0.

- [ ] **Step 7: Commit**

```bash
git add src/TimePersistenceStore.h src/TimePersistenceStore.cpp test/time_persistence/ test/run_time_persistence_test.sh
git commit -m "feat: TimePersistenceStore with CRC and atomic write"
```

---

### Task 2: TimeService formatting math — TDD

**Files:**
- Create: `src/services/TimeService.h` (interface only at this stage)
- Create: `src/services/TimeFormat.h` / `.cpp` (the pure-logic formatter, host-buildable)
- Create: `test/time_format/test_time_format.cpp`
- Create: `test/run_time_format_test.sh`

We extract the formatting math into `TimeFormat` so it has zero hardware deps and the tests don't have to mock anything.

- [ ] **Step 1: Write the failing test file**

```cpp
// test/time_format/test_time_format.cpp
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
  // 1735689600 = 2025-01-01 00:00 UTC. offset -7 → previous day 17:00 local.
  char buf[16];
  ASSERT_TRUE(TimeFormat::format(1735689600, -7, true, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "17:00");
  PASS();
}

static void test_12h_render() {
  char buf[16];
  // 00:00 → "12:00 AM"
  ASSERT_TRUE(TimeFormat::format(1735689600, 0, false, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "12:00 AM");
  // 12:00 → "12:00 PM"
  ASSERT_TRUE(TimeFormat::format(1735689600 + 12*3600, 0, false, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "12:00 PM");
  // 13:05 → "1:05 PM"
  ASSERT_TRUE(TimeFormat::format(1735689600 + 13*3600 + 5*60, 0, false, buf, sizeof(buf)));
  ASSERT_STREQ(buf, "1:05 PM");
  // 23:59 → "11:59 PM"
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
```

- [ ] **Step 2: Write the build/run script**

```bash
#!/usr/bin/env bash
# test/run_time_format_test.sh
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/time_format"
BINARY="$BUILD_DIR/TimeFormatTest"
mkdir -p "$BUILD_DIR"
SOURCES=(
  "$ROOT_DIR/test/time_format/test_time_format.cpp"
  "$ROOT_DIR/src/services/TimeFormat.cpp"
)
CXXFLAGS=(-std=c++20 -O2 -Wall -Wextra -pedantic -I"$ROOT_DIR")
c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"
"$BINARY" "$@"
```

```bash
chmod +x test/run_time_format_test.sh
```

- [ ] **Step 3: Run script — confirm compile error**

Run: `bash test/run_time_format_test.sh`
Expected: compile fails — `TimeFormat.h` does not exist yet.

- [ ] **Step 4: Implement `TimeFormat.h`**

```cpp
// src/services/TimeFormat.h
#pragma once
#include <cstddef>
#include <cstdint>

namespace TimeFormat {

constexpr int64_t kMinValidEpoch = 1735689600;   // 2025-01-01T00:00:00Z
constexpr int64_t kMaxValidEpoch = 4102444800;   // 2100-01-01T00:00:00Z

// Returns true on success and writes a NUL-terminated string to `out`.
// `format24h=true` → "HH:MM" (5 chars). `format24h=false` → "H:MM AM/PM" (up to 8 chars).
// Returns false if `epoch` is outside the sanity bounds, or if the buffer is too small.
bool format(int64_t utcEpoch, int offsetHours, bool format24h, char* out, size_t cap);

}  // namespace TimeFormat
```

- [ ] **Step 5: Implement `TimeFormat.cpp`**

```cpp
// src/services/TimeFormat.cpp
#include "TimeFormat.h"
#include <cstdio>

namespace TimeFormat {

bool format(int64_t utcEpoch, int offsetHours, bool format24h, char* out, size_t cap) {
  if (utcEpoch < kMinValidEpoch || utcEpoch > kMaxValidEpoch) return false;
  if (out == nullptr || cap == 0) return false;

  const int64_t local = utcEpoch + int64_t(offsetHours) * 3600;
  // Guard against negative modulo by adding a multiple of 86400 large enough to be positive.
  // local can never be negative here since offsetHours is bounded [-12, 14] and utcEpoch >= 2025-01-01.
  const int64_t secondsOfDay = ((local % 86400) + 86400) % 86400;
  const int hour24 = int(secondsOfDay / 3600);
  const int minute = int((secondsOfDay / 60) % 60);

  int n = 0;
  if (format24h) {
    n = snprintf(out, cap, "%02d:%02d", hour24, minute);
  } else {
    const int hour12 = ((hour24 + 11) % 12) + 1;
    const char* ampm = hour24 < 12 ? "AM" : "PM";
    n = snprintf(out, cap, "%d:%02d %s", hour12, minute, ampm);
  }
  if (n <= 0 || size_t(n) >= cap) return false;
  return true;
}

}  // namespace TimeFormat
```

- [ ] **Step 6: Run tests and verify all pass**

Run: `bash test/run_time_format_test.sh`
Expected: `Passed: 9, Failed: 0`, exit code 0.

- [ ] **Step 7: Commit**

```bash
git add src/services/TimeFormat.h src/services/TimeFormat.cpp test/time_format/ test/run_time_format_test.sh
git commit -m "feat: TimeFormat host-tested formatting helper"
```

---

### Task 3: Combined test runner

**Files:**
- Create: `test/run_time_tests.sh`

- [ ] **Step 1: Write `run_time_tests.sh`**

```bash
#!/usr/bin/env bash
# test/run_time_tests.sh
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bash "$DIR/run_time_format_test.sh"
bash "$DIR/run_time_persistence_test.sh"
echo "All time tests passed."
```

```bash
chmod +x test/run_time_tests.sh
```

- [ ] **Step 2: Run it**

Run: `bash test/run_time_tests.sh`
Expected: both inner runs print `Passed: N, Failed: 0`, then `All time tests passed.`

- [ ] **Step 3: Commit**

```bash
git add test/run_time_tests.sh
git commit -m "test: combined time tests runner"
```

---

### Task 4: RtcBackend interface + HalRtcInternal

**Files:**
- Create: `lib/hal/HalRtc.h`
- Create: `lib/hal/HalRtcInternal.h`
- Create: `lib/hal/HalRtcInternal.cpp`

`HalRtcInternal` wraps newlib's `gettimeofday`/`settimeofday`, which on the ESP32 talks to the internal RTC. On the simulator host, the same calls work (read returns host wall time; write requires no privileges with `settimeofday` only when no time-zone arg, but to keep simulator and device behavior in sync we keep an in-process offset like the spec describes).

- [ ] **Step 1: Write `HalRtc.h`**

```cpp
// lib/hal/HalRtc.h
#pragma once
#include <cstdint>

class RtcBackend {
 public:
  virtual ~RtcBackend() = default;
  // Returns true on success and writes the current UTC epoch (seconds) to *out.
  // Returns false if the underlying clock is uninitialized or unreadable.
  virtual bool readUtcEpoch(int64_t* out) = 0;
  // Returns true if write succeeded.
  virtual bool writeUtcEpoch(int64_t utcEpoch) = 0;
  // Sentinel: returns true iff the most recent readUtcEpoch() returned a value >= kMinValidEpoch.
  virtual bool hasValidTime() = 0;
};
```

- [ ] **Step 2: Write `HalRtcInternal.h`**

```cpp
// lib/hal/HalRtcInternal.h
#pragma once
#include "HalRtc.h"

class HalRtcInternal : public RtcBackend {
 public:
  bool readUtcEpoch(int64_t* out) override;
  bool writeUtcEpoch(int64_t utcEpoch) override;
  bool hasValidTime() override;
};
```

- [ ] **Step 3: Write `HalRtcInternal.cpp`**

```cpp
// lib/hal/HalRtcInternal.cpp
#include "HalRtcInternal.h"

#include <sys/time.h>

#include "src/services/TimeFormat.h"  // for kMinValidEpoch

bool HalRtcInternal::readUtcEpoch(int64_t* out) {
  if (out == nullptr) return false;
  timeval tv;
  if (gettimeofday(&tv, nullptr) != 0) return false;
  *out = int64_t(tv.tv_sec);
  return *out >= TimeFormat::kMinValidEpoch;
}

bool HalRtcInternal::writeUtcEpoch(int64_t utcEpoch) {
  timeval tv{ time_t(utcEpoch), 0 };
  return settimeofday(&tv, nullptr) == 0;
}

bool HalRtcInternal::hasValidTime() {
  int64_t epoch;
  return readUtcEpoch(&epoch);
}
```

- [ ] **Step 4: Verify compiles in firmware build**

Run: `pio run -e tiny 2>&1 | tail -20`
Expected: no errors mentioning `HalRtcInternal` or `HalRtc`. Existing errors unrelated to these files are fine — we'll wire it up in later tasks.

- [ ] **Step 5: Commit**

```bash
git add lib/hal/HalRtc.h lib/hal/HalRtcInternal.h lib/hal/HalRtcInternal.cpp
git commit -m "feat: RtcBackend interface and internal RTC backend"
```

---

### Task 5: HalRtcDS3231 driver

**Files:**
- Create: `lib/hal/HalRtcDS3231.h`
- Create: `lib/hal/HalRtcDS3231.cpp`

DS3231 BCD time registers start at `0x00`: seconds, minutes, hours (24h mode when bit 6 = 0), day-of-week, day, month (with century bit), year. We always set the hour register in 24h mode (bit 6 = 0). The existing `HalGPIO::probeDS3231Signature` (`lib/hal/HalGPIO.cpp:78`) verifies BCD seconds < 60 — reuse the same `Wire` access pattern. The `Wire` instance must be the X3 I2C bus (see `X3_I2C_SDA`, `X3_I2C_SCL`, `X3_I2C_FREQ` in `HalGPIO.cpp`).

- [ ] **Step 1: Write `HalRtcDS3231.h`**

```cpp
// lib/hal/HalRtcDS3231.h
#pragma once
#include "HalRtc.h"

class HalRtcDS3231 : public RtcBackend {
 public:
  // Construct and probe. If the chip does not respond, isAvailable() returns false.
  HalRtcDS3231();

  bool isAvailable() const { return available_; }
  bool readUtcEpoch(int64_t* out) override;
  bool writeUtcEpoch(int64_t utcEpoch) override;
  bool hasValidTime() override;

 private:
  bool available_ = false;
};
```

- [ ] **Step 2: Write `HalRtcDS3231.cpp`**

```cpp
// lib/hal/HalRtcDS3231.cpp
#include "HalRtcDS3231.h"

#include <Wire.h>
#include <time.h>

#include "HalGPIO.h"
#include "Logging.h"
#include "src/services/TimeFormat.h"

namespace {
constexpr uint8_t kAddr = I2C_ADDR_DS3231;  // 0x68
constexpr uint8_t kSecReg = DS3231_SEC_REG;  // 0x00

uint8_t bcdToBin(uint8_t v) { return uint8_t((v >> 4) * 10 + (v & 0x0F)); }
uint8_t binToBcd(uint8_t v) { return uint8_t(((v / 10) << 4) | (v % 10)); }

// Returns true if Wire transactions succeed and seconds-register is plausible BCD.
bool probe() {
  Wire.beginTransmission(kAddr);
  Wire.write(kSecReg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(kAddr, uint8_t(1)) != 1) return false;
  uint8_t sec = Wire.read();
  uint8_t tens = (sec >> 4) & 0x07;
  uint8_t ones = sec & 0x0F;
  return tens <= 5 && ones <= 9;
}
}  // namespace

HalRtcDS3231::HalRtcDS3231() {
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);
  available_ = probe();
  if (!available_) {
    LOG_ERR("RTC", "DS3231 probe failed");
  }
}

bool HalRtcDS3231::readUtcEpoch(int64_t* out) {
  if (!available_ || out == nullptr) return false;
  Wire.beginTransmission(kAddr);
  Wire.write(kSecReg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(kAddr, uint8_t(7)) != 7) return false;

  uint8_t b[7];
  for (int i = 0; i < 7; ++i) b[i] = Wire.read();

  tm t{};
  t.tm_sec  = bcdToBin(b[0] & 0x7F);
  t.tm_min  = bcdToBin(b[1] & 0x7F);
  t.tm_hour = bcdToBin(b[2] & 0x3F);  // assumes 24h mode (bit 6 = 0)
  // b[3] = day of week (1..7) — ignored
  t.tm_mday = bcdToBin(b[4] & 0x3F);
  uint8_t monthRaw = b[5];
  t.tm_mon  = bcdToBin(monthRaw & 0x1F) - 1;  // tm_mon is 0..11
  int year = bcdToBin(b[6]) + 2000;
  if (monthRaw & 0x80) year += 100;  // century bit (we'll never see this in practice)
  t.tm_year = year - 1900;

  // Use timegm if available; otherwise compute via mktime with TZ=UTC.
  // ESP-IDF newlib provides timegm.
  time_t epoch = timegm(&t);
  if (epoch < 0) return false;
  *out = int64_t(epoch);
  return *out >= TimeFormat::kMinValidEpoch;
}

bool HalRtcDS3231::writeUtcEpoch(int64_t utcEpoch) {
  if (!available_) return false;
  time_t epoch = time_t(utcEpoch);
  tm t{};
  gmtime_r(&epoch, &t);

  // Century bit: set when year >= 2100. Won't happen in practice.
  uint8_t monthByte = binToBcd(uint8_t(t.tm_mon + 1));
  if (t.tm_year + 1900 >= 2100) monthByte |= 0x80;

  Wire.beginTransmission(kAddr);
  Wire.write(kSecReg);
  Wire.write(binToBcd(uint8_t(t.tm_sec)));
  Wire.write(binToBcd(uint8_t(t.tm_min)));
  Wire.write(binToBcd(uint8_t(t.tm_hour)));        // 24h mode (bit 6 = 0)
  Wire.write(uint8_t(t.tm_wday + 1));              // 1..7
  Wire.write(binToBcd(uint8_t(t.tm_mday)));
  Wire.write(monthByte);
  Wire.write(binToBcd(uint8_t((t.tm_year + 1900) % 100)));
  if (Wire.endTransmission() != 0) {
    LOG_ERR("RTC", "DS3231 write failed");
    return false;
  }
  return true;
}

bool HalRtcDS3231::hasValidTime() {
  int64_t epoch;
  return readUtcEpoch(&epoch);
}
```

- [ ] **Step 3: Verify firmware build**

Run: `pio run -e tiny 2>&1 | tail -20`
Expected: no errors mentioning `HalRtcDS3231`.

- [ ] **Step 4: Commit**

```bash
git add lib/hal/HalRtcDS3231.h lib/hal/HalRtcDS3231.cpp
git commit -m "feat: DS3231 RTC backend (X3)"
```

---

### Task 6: NtpSyncService — extract from KOReader

**Files:**
- Create: `src/services/NtpSyncService.h`
- Create: `src/services/NtpSyncService.cpp`
- Modify: `src/activities/reader/KOReaderSyncActivity.cpp` (remove inline `syncTimeWithNTP`/`wifiOff`, call into the service)

The existing inline code at `src/activities/reader/KOReaderSyncActivity.cpp:25-58` becomes the implementation, generalized to return result + epoch and to support cooperative cancel.

- [ ] **Step 1: Write `NtpSyncService.h`**

```cpp
// src/services/NtpSyncService.h
#pragma once
#include <cstdint>
#include <atomic>

class NtpSyncService {
 public:
  enum class Error : uint8_t { None, NoCredentials, WifiConnectFailed, NtpTimeout, BadEpoch };

  struct Result {
    bool ok;
    int64_t epoch;   // valid only if ok
    Error error;     // None if ok
  };

  static NtpSyncService& instance();

  // Connect to lastConnectedSsid (or first credential), run SNTP, return result.
  // Disconnects Wi-Fi before returning regardless of success.
  // wifiTimeoutMs: how long to wait for WL_CONNECTED before giving up.
  // ntpTimeoutMs: how long to wait for SNTP_SYNC_STATUS_COMPLETED before giving up.
  Result syncOnce(uint32_t wifiTimeoutMs = 8000, uint32_t ntpTimeoutMs = 5000);

  // Cooperative cancel: poll loop checks this each ~100 ms tick.
  void cancel() { cancelFlag_.store(true); }
  void resetCancel() { cancelFlag_.store(false); }

 private:
  NtpSyncService() = default;
  std::atomic<bool> cancelFlag_{false};
};
```

- [ ] **Step 2: Write `NtpSyncService.cpp`**

```cpp
// src/services/NtpSyncService.cpp
#include "NtpSyncService.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "Logging.h"
#include "WifiCredentialStore.h"
#include "TimeFormat.h"

NtpSyncService& NtpSyncService::instance() {
  static NtpSyncService s;
  return s;
}

namespace {
const WifiCredential* pickCredential() {
  auto& store = WifiCredentialStore::instance;
  if (store.credentials.empty()) return nullptr;
  const std::string& last = store.lastConnectedSsid;
  if (!last.empty()) {
    if (auto* c = store.findCredential(last)) return c;
  }
  return &store.credentials.front();
}

void wifiOff() {
  if (esp_sntp_enabled()) esp_sntp_stop();
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}
}  // namespace

NtpSyncService::Result NtpSyncService::syncOnce(uint32_t wifiTimeoutMs, uint32_t ntpTimeoutMs) {
  resetCancel();

  const WifiCredential* cred = pickCredential();
  if (cred == nullptr) {
    LOG_INF("NTP", "no saved Wi-Fi credentials");
    return { false, 0, Error::NoCredentials };
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());

  uint32_t waited = 0;
  while (WiFi.status() != WL_CONNECTED && waited < wifiTimeoutMs) {
    if (cancelFlag_.load()) { wifiOff(); return { false, 0, Error::WifiConnectFailed }; }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    waited += 100;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("NTP", "Wi-Fi connect failed (%s)", cred->ssid.c_str());
    wifiOff();
    return { false, 0, Error::WifiConnectFailed };
  }

  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  waited = 0;
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && waited < ntpTimeoutMs) {
    if (cancelFlag_.load()) { wifiOff(); return { false, 0, Error::NtpTimeout }; }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    waited += 100;
  }

  if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    LOG_ERR("NTP", "SNTP timeout");
    wifiOff();
    return { false, 0, Error::NtpTimeout };
  }

  timeval tv;
  gettimeofday(&tv, nullptr);
  int64_t epoch = int64_t(tv.tv_sec);
  wifiOff();

  if (epoch < TimeFormat::kMinValidEpoch || epoch > TimeFormat::kMaxValidEpoch) {
    LOG_ERR("NTP", "absurd epoch %lld", static_cast<long long>(epoch));
    return { false, 0, Error::BadEpoch };
  }
  LOG_INF("NTP", "synced epoch=%lld", static_cast<long long>(epoch));
  return { true, epoch, Error::None };
}
```

- [ ] **Step 3: Switch `KOReaderSyncActivity` to use the service**

Open `src/activities/reader/KOReaderSyncActivity.cpp`. Delete the entire anonymous namespace block at lines 24-59 (`syncTimeWithNTP` and `wifiOff`). Add include `#include "services/NtpSyncService.h"`. Find the call site at line 111 (`syncTimeWithNTP();`) and replace it with:

```cpp
NtpSyncService::instance().syncOnce();
```

Keep the surrounding "Sync time with NTP before making API requests" comment.

- [ ] **Step 4: Build firmware**

Run: `pio run -e tiny 2>&1 | tail -20`
Expected: no errors. KOReader still compiles; NTP behavior unchanged from user perspective.

- [ ] **Step 5: Commit**

```bash
git add src/services/NtpSyncService.h src/services/NtpSyncService.cpp src/activities/reader/KOReaderSyncActivity.cpp
git commit -m "refactor: extract NtpSyncService from KOReaderSyncActivity"
```

---

### Task 7: TimeService — boot, source flag, mutex

**Files:**
- Create: `src/services/TimeService.h`
- Create: `src/services/TimeService.cpp`

`TimeService` owns the `RtcBackend`, the `TimePersistenceStore`, the `TimeSource` flag, and a FreeRTOS mutex. It exposes `formatLocal` for the header read path, `onNtpSynced`/`onManualSet` for write paths, and `boot(deviceType)` for `main.cpp`.

The boot path needs `TimePersistenceStore::Filesystem` wired to `HalStorage`. We declare a small adapter in this same file.

- [ ] **Step 1: Write `TimeService.h`**

```cpp
// src/services/TimeService.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class RtcBackend;
class TimePersistenceStore;

enum class TimeSource : uint8_t { None, RestoredFromNvs, NtpSynced, ManuallySet };

class TimeService {
 public:
  // HalGPIO::DeviceType is an enum class, but we avoid the include here — pass an int.
  // 0 = X4, 1 = X3 (matches HalGPIO::DeviceType numeric values).
  enum class DeviceType : uint8_t { X4 = 0, X3 = 1 };

  static TimeService& instance();

  // One-time setup. Selects backend, restores from NVS if needed, optionally spawns
  // the cold-boot NTP task.
  void boot(DeviceType deviceType);

  // Header read path. Returns true on success and writes a NUL-terminated string.
  // Suppresses write if there is no valid time.
  bool formatLocal(char* out, size_t cap);

  bool hasValidTime();

  // Called from the cold-boot NTP task and from the "Sync now" popup.
  // boot-task path passes ignoreManualGuard=false; sync-now passes true.
  void onNtpSynced(int64_t epoch, bool ignoreManualGuard);

  // Called from SetTimeActivity OK and from the web POST /api/time handler.
  void onManualSet(int64_t epoch);

  TimeSource currentSource();

 private:
  TimeService() = default;
  ~TimeService() = default;

  std::unique_ptr<RtcBackend> rtc_;
  std::unique_ptr<TimePersistenceStore> persistence_;
  TimeSource source_ = TimeSource::None;
  SemaphoreHandle_t mutex_ = nullptr;

  static void coldBootNtpTask(void* arg);
};
```

- [ ] **Step 2: Write `TimeService.cpp`**

```cpp
// src/services/TimeService.cpp
#include "TimeService.h"

#include <HalStorage.h>

#include "CrossPointSettings.h"
#include "Logging.h"
#include "TimeFormat.h"
#include "TimePersistenceStore.h"
#include "WifiCredentialStore.h"
#include "lib/hal/HalRtc.h"
#include "lib/hal/HalRtcDS3231.h"
#include "lib/hal/HalRtcInternal.h"
#include "NtpSyncService.h"

namespace {

// Adapter: TimePersistenceStore::Filesystem -> HalStorage.
class HalStorageFs : public TimePersistenceStore::Filesystem {
 public:
  bool exists(const char* p) override { return Storage.exists(p); }
  bool remove(const char* p) override { return Storage.remove(p); }
  bool rename(const char* o, const char* n) override { return Storage.rename(o, n); }
  bool read(const char* p, uint8_t* out, size_t cap, size_t* outLen) override {
    FsFile f;
    if (!Storage.openFileForRead("TPS", p, f)) return false;
    size_t n = f.read(out, cap);
    f.close();
    if (n == 0) return false;
    *outLen = n;
    return true;
  }
  bool write(const char* p, const uint8_t* data, size_t len) override {
    Storage.mkdir("/.crosspoint");
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FsFile f;
    if (!Storage.openFileForWrite("TPS", tmp, f)) return false;
    size_t w = f.write(data, len);
    f.flush();
    f.close();
    if (w != len) { Storage.remove(tmp); return false; }
    if (Storage.exists(p)) Storage.remove(p);
    return Storage.rename(tmp, p);
  }
};

HalStorageFs& fs() {
  static HalStorageFs s;
  return s;
}

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t m) : m_(m) { if (m_) xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { if (m_) xSemaphoreGive(m_); }
 private:
  SemaphoreHandle_t m_;
};

}  // namespace

TimeService& TimeService::instance() {
  static TimeService s;
  return s;
}

void TimeService::boot(DeviceType deviceType) {
  if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();

  // Select backend
  if (deviceType == DeviceType::X3) {
    auto ds = std::make_unique<HalRtcDS3231>();
    if (ds->isAvailable()) {
      rtc_ = std::move(ds);
    } else {
      LOG_ERR("RTC", "DS3231 unavailable, falling back to internal");
      rtc_ = std::make_unique<HalRtcInternal>();
    }
  } else {
    rtc_ = std::make_unique<HalRtcInternal>();
  }

  // Load persistence
  persistence_ = std::make_unique<TimePersistenceStore>(fs());
  persistence_->load();

  // Decide initial source
  if (rtc_->hasValidTime()) {
    source_ = (deviceType == DeviceType::X3) ? TimeSource::NtpSynced
                                             : TimeSource::RestoredFromNvs;
  } else if (persistence_->hasLastSynced()) {
    rtc_->writeUtcEpoch(persistence_->lastSyncedUtc());
    source_ = TimeSource::RestoredFromNvs;
  } else {
    source_ = TimeSource::None;
  }

  // Spawn cold-boot NTP task if needed
  const bool needsBootNtp =
      (deviceType == DeviceType::X4) ||
      (deviceType == DeviceType::X3 && !rtc_->hasValidTime());
  if (needsBootNtp && !WifiCredentialStore::instance.credentials.empty()) {
    xTaskCreate(&TimeService::coldBootNtpTask, "ntp_boot", 4096, nullptr,
                tskIDLE_PRIORITY + 1, nullptr);
  }
}

void TimeService::coldBootNtpTask(void* /*arg*/) {
  auto result = NtpSyncService::instance().syncOnce();
  if (result.ok) {
    TimeService::instance().onNtpSynced(result.epoch, /*ignoreManualGuard=*/false);
  }
  vTaskDelete(nullptr);
}

bool TimeService::formatLocal(char* out, size_t cap) {
  Lock g(mutex_);
  if (rtc_ == nullptr) return false;
  int64_t epoch;
  if (!rtc_->readUtcEpoch(&epoch)) return false;
  const int offset = int(SETTINGS.utcOffsetIndex) - 12;  // 0..26 → -12..+14
  return TimeFormat::format(epoch, offset, SETTINGS.timeFormat == 0, out, cap);
}

bool TimeService::hasValidTime() {
  Lock g(mutex_);
  return rtc_ != nullptr && rtc_->hasValidTime();
}

void TimeService::onNtpSynced(int64_t epoch, bool ignoreManualGuard) {
  Lock g(mutex_);
  if (!ignoreManualGuard && source_ == TimeSource::ManuallySet) return;
  if (rtc_ == nullptr) return;
  rtc_->writeUtcEpoch(epoch);
  if (persistence_) persistence_->write(epoch);
  source_ = TimeSource::NtpSynced;
}

void TimeService::onManualSet(int64_t epoch) {
  Lock g(mutex_);
  if (rtc_ == nullptr) return;
  rtc_->writeUtcEpoch(epoch);
  if (persistence_) persistence_->write(epoch);
  source_ = TimeSource::ManuallySet;
}

TimeSource TimeService::currentSource() {
  Lock g(mutex_);
  return source_;
}
```

Note the use of `SETTINGS.utcOffsetIndex` and `SETTINGS.timeFormat` — those fields are added in Task 8.

- [ ] **Step 3: Build firmware (will fail until Task 8 lands)**

Run: `pio run -e tiny 2>&1 | tail -20`
Expected: errors about `utcOffsetIndex`/`timeFormat` not being members of `CrossPointSettings`. That's expected — we land them in Task 8 and the build comes back clean.

- [ ] **Step 4: Commit (deferred)**

Do not commit yet — wait until Task 8 makes the build green again. The combined commit will land in Task 8.

---

### Task 8: CrossPointSettings additions + clamps

**Files:**
- Modify: `src/CrossPointSettings.h`
- Modify: `src/CrossPointSettings.cpp` (clamp on load)

`SettingInfo::valuePtr` is `uint8_t CrossPointSettings::*`, so all three new fields must be `uint8_t`. We store the timezone as a 0..26 index where `0 → UTC-12` and `26 → UTC+14`; the canonical default is `12` (UTC+0).

- [ ] **Step 1: Add the fields to `CrossPointSettings.h`**

Find the existing fields (look near the `// 0 = portrait …` comment around line 256). Add these three fields anywhere logical (e.g., next to `hideBatteryPercentage`):

```cpp
// Time / clock display
uint8_t showHeaderClock = 1;     // 0 = hidden, 1 = shown
uint8_t timeFormat = 0;          // 0 = 24h, 1 = 12h
uint8_t utcOffsetIndex = 12;     // 0..26 → UTC-12..UTC+14 (12 = UTC+0)
```

- [ ] **Step 2: Add clamp on load in `CrossPointSettings.cpp`**

Open `src/CrossPointSettings.cpp`, find the function that runs after `load()` (or the `load()` method itself) — the existing settings file already does post-load clamps for things like `sleepScreen`. Add at the end of the post-load clamp section:

```cpp
if (showHeaderClock > 1) showHeaderClock = 1;
if (timeFormat > 1) timeFormat = 0;
if (utcOffsetIndex > 26) utcOffsetIndex = 12;
```

If there is no existing post-load clamp section, add one in the appropriate place (typically `CrossPointSettings::load()` after the JSON parse).

- [ ] **Step 3: Build**

Run: `pio run -e tiny 2>&1 | tail -20`
Expected: no errors related to time. `TimeService.cpp` from Task 7 now compiles.

- [ ] **Step 4: Commit Tasks 7 + 8 together**

```bash
git add src/services/TimeService.h src/services/TimeService.cpp src/CrossPointSettings.h src/CrossPointSettings.cpp
git commit -m "feat: TimeService and settings fields for clock display"
```

---

### Task 9: i18n strings (english.yaml)

**Files:**
- Modify: `lib/I18n/translations/english.yaml`

After editing yaml, regenerate via `python scripts/gen_i18n.py` (per CLAUDE.md "Generated Files" rule).

- [ ] **Step 1: Add the fixed strings to `english.yaml`**

Append (or merge into the appropriate section — match the existing alphabetical/topical pattern):

```yaml
STR_CAT_TIME: Time
STR_SHOW_HEADER_CLOCK: Show clock in header
STR_TIME_FORMAT: Time format
STR_TIME_FORMAT_24H: 24-hour
STR_TIME_FORMAT_12H: 12-hour
STR_TIMEZONE: Time zone
STR_SYNC_TIME_NOW: Sync time now
STR_SET_TIME_MANUAL: Set time manually
STR_SYNCING: "Syncing time…"
STR_CONNECTING_WIFI: "Connecting to Wi-Fi…"
STR_SYNC_FAILED: Sync failed
STR_SYNC_OK: Time synced
STR_NO_SAVED_WIFI: No saved Wi-Fi network
STR_COULDNT_UPDATE_RTC: Couldn't update RTC
STR_TIME_FIELD_HOURS: Hours
STR_TIME_FIELD_MINUTES: Minutes
```

- [ ] **Step 2: Add the 27 timezone labels**

```yaml
STR_TZ_UTC_M12: UTC-12
STR_TZ_UTC_M11: UTC-11
STR_TZ_UTC_M10: UTC-10
STR_TZ_UTC_M9:  UTC-9
STR_TZ_UTC_M8:  UTC-8
STR_TZ_UTC_M7:  UTC-7
STR_TZ_UTC_M6:  UTC-6
STR_TZ_UTC_M5:  UTC-5
STR_TZ_UTC_M4:  UTC-4
STR_TZ_UTC_M3:  UTC-3
STR_TZ_UTC_M2:  UTC-2
STR_TZ_UTC_M1:  UTC-1
STR_TZ_UTC_0:   UTC+0
STR_TZ_UTC_P1:  UTC+1
STR_TZ_UTC_P2:  UTC+2
STR_TZ_UTC_P3:  UTC+3
STR_TZ_UTC_P4:  UTC+4
STR_TZ_UTC_P5:  UTC+5
STR_TZ_UTC_P6:  UTC+6
STR_TZ_UTC_P7:  UTC+7
STR_TZ_UTC_P8:  UTC+8
STR_TZ_UTC_P9:  UTC+9
STR_TZ_UTC_P10: UTC+10
STR_TZ_UTC_P11: UTC+11
STR_TZ_UTC_P12: UTC+12
STR_TZ_UTC_P13: UTC+13
STR_TZ_UTC_P14: UTC+14
```

- [ ] **Step 3: Regenerate the i18n header**

Run: `python scripts/gen_i18n.py`
Expected: prints success and updates files under `lib/I18n/`. No errors about duplicate keys.

- [ ] **Step 4: Build**

Run: `pio run -e tiny 2>&1 | tail -10`
Expected: clean build. The new `StrId::STR_*` enum values now exist.

- [ ] **Step 5: Commit**

```bash
git add lib/I18n/translations/english.yaml lib/I18n/
git commit -m "i18n: add Time category strings and 27 UTC offset labels"
```

---

### Task 10: SettingAction enum entries

**Files:**
- Modify: `src/activities/settings/SettingsActivity.h`

- [ ] **Step 1: Add the two new enum values**

In `src/activities/settings/SettingsActivity.h:14-27`, extend `enum class SettingAction`:

```cpp
enum class SettingAction {
  None,
  RemapFrontButtons,
  RemapFrontButtonsReader,
  CustomiseStatusBar,
  KOReaderSync,
  OPDSBrowser,
  Network,
  ClearCache,
  // CheckForUpdates,
  // SdFirmwareUpdate,
  Language,
  DownloadFonts,
  SyncTimeNow,        // new
  SetTimeManual,      // new
};
```

- [ ] **Step 2: Build**

Run: `pio run -e tiny 2>&1 | tail -5`
Expected: clean (the `case SettingAction::None:` switch in `SettingsActivity.cpp` has a `default:` or trailing case; if it issues a `-Wswitch` warning about the new enum values not being handled, that's expected and is fixed in Task 13. Note the warning so Task 13 confirms it goes away.)

- [ ] **Step 3: No commit yet** — folded into Task 11.

---

### Task 11: SettingsList — Time category and entries

**Files:**
- Modify: `src/SettingsList.h`

- [ ] **Step 1: Add a `STR_CAT_TIME` category and the 5 entries**

In `src/SettingsList.h`, find the location where category-tagged entries are pushed (look for `StrId::STR_CAT_DISPLAY` for the pattern). Add after the Display group, before the Reader group:

```cpp
// === Time ===
SettingInfo::Toggle(StrId::STR_SHOW_HEADER_CLOCK, &CrossPointSettings::showHeaderClock,
                    "showHeaderClock", StrId::STR_CAT_TIME),
SettingInfo::Enum(StrId::STR_TIME_FORMAT, &CrossPointSettings::timeFormat,
                  {StrId::STR_TIME_FORMAT_24H, StrId::STR_TIME_FORMAT_12H},
                  "timeFormat", StrId::STR_CAT_TIME),
SettingInfo::Enum(StrId::STR_TIMEZONE, &CrossPointSettings::utcOffsetIndex,
                  {
                    StrId::STR_TZ_UTC_M12, StrId::STR_TZ_UTC_M11, StrId::STR_TZ_UTC_M10,
                    StrId::STR_TZ_UTC_M9,  StrId::STR_TZ_UTC_M8,  StrId::STR_TZ_UTC_M7,
                    StrId::STR_TZ_UTC_M6,  StrId::STR_TZ_UTC_M5,  StrId::STR_TZ_UTC_M4,
                    StrId::STR_TZ_UTC_M3,  StrId::STR_TZ_UTC_M2,  StrId::STR_TZ_UTC_M1,
                    StrId::STR_TZ_UTC_0,
                    StrId::STR_TZ_UTC_P1,  StrId::STR_TZ_UTC_P2,  StrId::STR_TZ_UTC_P3,
                    StrId::STR_TZ_UTC_P4,  StrId::STR_TZ_UTC_P5,  StrId::STR_TZ_UTC_P6,
                    StrId::STR_TZ_UTC_P7,  StrId::STR_TZ_UTC_P8,  StrId::STR_TZ_UTC_P9,
                    StrId::STR_TZ_UTC_P10, StrId::STR_TZ_UTC_P11, StrId::STR_TZ_UTC_P12,
                    StrId::STR_TZ_UTC_P13, StrId::STR_TZ_UTC_P14,
                  },
                  "utcOffsetIndex", StrId::STR_CAT_TIME),
SettingInfo::Action(StrId::STR_SYNC_TIME_NOW, SettingAction::SyncTimeNow),
SettingInfo::Action(StrId::STR_SET_TIME_MANUAL, SettingAction::SetTimeManual),
```

The two `Action` entries don't take a category argument (matching the existing `Action(STR_WIFI_NETWORKS, …)` pattern in `SettingsActivity.cpp`). They render under their own grouping by the existing settings-list logic.

- [ ] **Step 2: Build**

Run: `pio run -e tiny 2>&1 | tail -5`
Expected: clean.

- [ ] **Step 3: Commit Tasks 10 + 11**

```bash
git add src/activities/settings/SettingsActivity.h src/SettingsList.h
git commit -m "feat: add Time category and Sync/Set actions to settings"
```

---

### Task 12: SyncTimeNowActivity — modal popup

**Files:**
- Create: `src/activities/settings/SyncTimeNowActivity.h`
- Create: `src/activities/settings/SyncTimeNowActivity.cpp`

This activity owns the foreground sync. It runs the sync **inline on its own loop** (calling `NtpSyncService::syncOnce` from `loop()` after the first render so the user sees the "Connecting…" message before the blocking call begins), and routes Back to `NtpSyncService::cancel()`.

- [ ] **Step 1: Write the header**

```cpp
// src/activities/settings/SyncTimeNowActivity.h
#pragma once
#include <string>

#include "activities/Activity.h"

class SyncTimeNowActivity final : public Activity {
 public:
  SyncTimeNowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SyncTimeNow", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Phase : uint8_t { Idle, Running, NoCreds, Failed, OkBriefly };
  Phase phase_ = Phase::Idle;
  std::string message_;
  uint32_t okShownAtMs_ = 0;
  bool kicked_ = false;
};
```

- [ ] **Step 2: Write the implementation**

```cpp
// src/activities/settings/SyncTimeNowActivity.cpp
#include "SyncTimeNowActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "services/NtpSyncService.h"
#include "services/TimeService.h"

void SyncTimeNowActivity::onEnter() {
  phase_ = Phase::Idle;
  message_ = I18N.get(StrId::STR_CONNECTING_WIFI);
  kicked_ = false;
}

void SyncTimeNowActivity::onExit() {
  // Always tear down Wi-Fi if a sync was in flight.
  NtpSyncService::instance().cancel();
}

void SyncTimeNowActivity::loop() {
  // Cancel on Back.
  if (mappedInput.consumeButtonPress(MappedInputManager::Button::Back)) {
    if (phase_ == Phase::Running) NtpSyncService::instance().cancel();
    finish();
    return;
  }

  if (phase_ == Phase::OkBriefly) {
    if (millis() - okShownAtMs_ > 1500) finish();
    return;
  }

  if (phase_ == Phase::NoCreds || phase_ == Phase::Failed) {
    if (mappedInput.consumeButtonPress(MappedInputManager::Button::Ok)) finish();
    return;
  }

  // Kick off the sync once, after the first render (so the popup is visible while we block).
  if (!kicked_ && phase_ == Phase::Idle) {
    kicked_ = true;
    phase_ = Phase::Running;
    requestRender();
    return;
  }

  if (phase_ == Phase::Running) {
    auto result = NtpSyncService::instance().syncOnce();
    if (!result.ok) {
      switch (result.error) {
        case NtpSyncService::Error::NoCredentials:
          message_ = I18N.get(StrId::STR_NO_SAVED_WIFI);
          phase_ = Phase::NoCreds;
          break;
        default:
          message_ = I18N.get(StrId::STR_SYNC_FAILED);
          phase_ = Phase::Failed;
          break;
      }
    } else {
      TimeService::instance().onNtpSynced(result.epoch, /*ignoreManualGuard=*/true);
      message_ = I18N.get(StrId::STR_SYNC_OK);
      phase_ = Phase::OkBriefly;
      okShownAtMs_ = millis();
    }
    requestRender();
  }
}

void SyncTimeNowActivity::render(RenderLock&&) {
  const int w = renderer.getWidth();
  const int h = renderer.getHeight();
  renderer.clear();
  GUI.drawHeader(renderer, Rect{0, 0, w, 60}, I18N.get(StrId::STR_SYNC_TIME_NOW));
  // Centered message
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message_.c_str(), EpdFontFamily::REGULAR);
  renderer.drawText(UI_12_FONT_ID, (w - textWidth) / 2, h / 2, message_.c_str(), true,
                    EpdFontFamily::REGULAR);
}
```

If `Activity` does not expose `requestRender()` / `finish()` / `consumeButtonPress` exactly under those names, look in `src/activities/Activity.h` and use the matching members (e.g., `markDirty()`, `requestExit()`, etc.). The shape of the activity loop stays the same.

- [ ] **Step 3: Build**

Run: `pio run -e tiny 2>&1 | tail -5`
Expected: clean. (If your activity API uses different method names, fix the calls — the design of this file is correct.)

- [ ] **Step 4: Commit (deferred)** — bundled with Task 13.

---

### Task 13: SetTimeActivity — on-device HH:MM editor

**Files:**
- Create: `src/activities/settings/SetTimeActivity.h`
- Create: `src/activities/settings/SetTimeActivity.cpp`

Pattern follows `ButtonRemapActivity` (heap-allocated activity, owns `ButtonNavigator`, button hints at bottom). Two integer fields (hours, minutes), 24h editor, OK saves, Back discards.

- [ ] **Step 1: Write the header**

```cpp
// src/activities/settings/SetTimeActivity.h
#pragma once
#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class SetTimeActivity final : public Activity {
 public:
  SetTimeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SetTime", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int field_ = 0;     // 0 = hours, 1 = minutes
  int hour_ = 0;
  int minute_ = 0;

  // dateGuess seconds-since-epoch at 00:00 UTC for the date used to compose the new epoch.
  int64_t dateGuessAtMidnightUtc_ = 0;

  void seedFromCurrentTime();
  int64_t composeUtcEpoch() const;
};
```

- [ ] **Step 2: Write the implementation**

```cpp
// src/activities/settings/SetTimeActivity.cpp
#include "SetTimeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <time.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "services/TimeService.h"

namespace {
constexpr int64_t kFallbackDateMidnightUtc = 1767225600;  // 2026-01-01T00:00:00Z

int64_t midnightOfDateUtc(int64_t epoch) {
  return epoch - (epoch % 86400);
}
}  // namespace

void SetTimeActivity::onEnter() {
  field_ = 0;
  seedFromCurrentTime();
}

void SetTimeActivity::onExit() {}

void SetTimeActivity::seedFromCurrentTime() {
  // Try to pull current local hour/minute from TimeService.
  char buf[16];
  bool seeded = false;
  if (TimeService::instance().formatLocal(buf, sizeof(buf))) {
    int h = 0, m = 0;
    if (sscanf(buf, "%d:%d", &h, &m) == 2) {
      hour_ = h;
      minute_ = m;
      seeded = true;
    }
  }
  if (!seeded) {
    hour_ = 0;
    minute_ = 0;
  }

  // Compute dateGuess: midnight of the date the RTC currently believes (UTC).
  // If RTC has nothing, fall back to 2026-01-01.
  // We re-read the underlying epoch via a helper exposed by TimeService — but to keep
  // TimeService's interface narrow we just call hasValidTime + format and reverse-derive.
  // Easier path: read internal epoch through a public helper. For now use the fallback if
  // hasValidTime() is false, otherwise use NOW's UTC midnight.
  if (TimeService::instance().hasValidTime()) {
    timeval tv;
    gettimeofday(&tv, nullptr);
    dateGuessAtMidnightUtc_ = midnightOfDateUtc(int64_t(tv.tv_sec));
  } else {
    dateGuessAtMidnightUtc_ = kFallbackDateMidnightUtc;
  }
}

int64_t SetTimeActivity::composeUtcEpoch() const {
  const int offsetHours = int(SETTINGS.utcOffsetIndex) - 12;
  // localEpoch = midnight + hh*3600 + mm*60. utcEpoch = local - offset*3600.
  const int64_t local = dateGuessAtMidnightUtc_ + int64_t(hour_) * 3600 + int64_t(minute_) * 60;
  return local - int64_t(offsetHours) * 3600;
}

void SetTimeActivity::loop() {
  if (mappedInput.consumeButtonPress(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.consumeButtonPress(MappedInputManager::Button::Left)) {
    field_ = 0;
    requestRender();
    return;
  }
  if (mappedInput.consumeButtonPress(MappedInputManager::Button::Right)) {
    field_ = 1;
    requestRender();
    return;
  }
  if (mappedInput.consumeButtonPress(MappedInputManager::Button::Up)) {
    if (field_ == 0) hour_   = (hour_   + 1) % 24;
    else             minute_ = (minute_ + 1) % 60;
    requestRender();
    return;
  }
  if (mappedInput.consumeButtonPress(MappedInputManager::Button::Down)) {
    if (field_ == 0) hour_   = (hour_   + 23) % 24;
    else             minute_ = (minute_ + 59) % 60;
    requestRender();
    return;
  }
  if (mappedInput.consumeButtonPress(MappedInputManager::Button::Ok)) {
    TimeService::instance().onManualSet(composeUtcEpoch());
    finish();
    return;
  }
}

void SetTimeActivity::render(RenderLock&&) {
  const int w = renderer.getWidth();
  const int h = renderer.getHeight();
  renderer.clear();
  GUI.drawHeader(renderer, Rect{0, 0, w, 60}, I18N.get(StrId::STR_SET_TIME_MANUAL));

  char hh[8]; snprintf(hh, sizeof(hh), "%02d", hour_);
  char mm[8]; snprintf(mm, sizeof(mm), "%02d", minute_);

  const int fontId = UI_12_FONT_ID;
  const int hwid = renderer.getTextWidth(fontId, hh, EpdFontFamily::BOLD);
  const int cwid = renderer.getTextWidth(fontId, ":",  EpdFontFamily::BOLD);
  const int mwid = renderer.getTextWidth(fontId, mm, EpdFontFamily::BOLD);
  const int total = hwid + cwid + mwid + 24;
  int x = (w - total) / 2;
  const int y = h / 2;

  // Hour
  if (field_ == 0) renderer.fillRect(x - 4, y - 4, hwid + 8, 36, false);
  renderer.drawText(fontId, x, y, hh, field_ != 0, EpdFontFamily::BOLD);
  x += hwid + 12;
  // Colon
  renderer.drawText(fontId, x, y, ":", true, EpdFontFamily::BOLD);
  x += cwid + 12;
  // Minute
  if (field_ == 1) renderer.fillRect(x - 4, y - 4, mwid + 8, 36, false);
  renderer.drawText(fontId, x, y, mm, field_ != 1, EpdFontFamily::BOLD);

  // Button hints — match the existing pattern; if there is a helper for "drawButtonHints",
  // use it. Inlined fallback:
  const char* hints = "<-> Field   ^v Adjust   OK Save   Back Cancel";
  renderer.drawText(SMALL_FONT_ID, 16, h - 24, hints, true, EpdFontFamily::REGULAR);
}
```

If your codebase has a helper for button hints (look for `drawButtonHints` or similar in `BaseTheme.cpp`), prefer it. The `MappedInputManager::Button` names (`Back`, `Ok`, `Left`, `Right`, `Up`, `Down`) match `ButtonRemapActivity` usage; if a different button maps to "Cancel" on this device, follow that mapping.

- [ ] **Step 3: Build**

Run: `pio run -e tiny 2>&1 | tail -5`
Expected: clean.

- [ ] **Step 4: Commit Tasks 12 + 13**

```bash
git add src/activities/settings/SyncTimeNowActivity.h src/activities/settings/SyncTimeNowActivity.cpp src/activities/settings/SetTimeActivity.h src/activities/settings/SetTimeActivity.cpp
git commit -m "feat: SyncTimeNow popup and on-device SetTime editor"
```

---

### Task 14: SettingsActivity — wire the two new actions

**Files:**
- Modify: `src/activities/settings/SettingsActivity.cpp`

- [ ] **Step 1: Add includes near top**

Add (after the existing settings includes):

```cpp
#include "SetTimeActivity.h"
#include "SyncTimeNowActivity.h"
```

- [ ] **Step 2: Add the two new switch cases**

Find the `switch (action)` block (around line 334). Add cases before `case SettingAction::None:`:

```cpp
case SettingAction::SyncTimeNow:
  startActivityForResult(std::make_unique<SyncTimeNowActivity>(renderer, mappedInput),
                         resultHandler);
  break;
case SettingAction::SetTimeManual:
  startActivityForResult(std::make_unique<SetTimeActivity>(renderer, mappedInput),
                         resultHandler);
  break;
```

(`resultHandler` mirrors the existing pattern used by `case SettingAction::RemapFrontButtons:` etc.)

- [ ] **Step 3: Build**

Run: `pio run -e tiny 2>&1 | tail -5`
Expected: clean. The `-Wswitch` warning from Task 10 (if any) is gone.

- [ ] **Step 4: Commit**

```bash
git add src/activities/settings/SettingsActivity.cpp
git commit -m "feat: wire SyncTimeNow and SetTimeManual settings actions"
```

---

### Task 15: Header rendering — Lyra and Base themes

**Files:**
- Modify: `src/components/themes/lyra/LyraTheme.cpp`
- Modify: `src/components/themes/BaseTheme.cpp`

Per CONTEXT.md, `LyraTheme::drawHeader` does **not** chain into `BaseTheme::drawHeader`. Both must be edited.

- [ ] **Step 1: Edit `LyraTheme::drawHeader`**

In `src/components/themes/lyra/LyraTheme.cpp::drawHeader` (starts at line 114), add at the top of the function (after `renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);`):

```cpp
// Header clock (Home only — Home passes title=nullptr).
if (title == nullptr && SETTINGS.showHeaderClock) {
  char clockBuf[16];
  if (TimeService::instance().formatLocal(clockBuf, sizeof(clockBuf))) {
    renderer.drawText(UI_12_FONT_ID,
                      rect.x + LyraMetrics::values.contentSidePadding,
                      rect.y + LyraMetrics::values.batteryBarHeight + 3,
                      clockBuf, true, EpdFontFamily::BOLD);
  }
}
```

Add includes near the top of the file:

```cpp
#include "CrossPointSettings.h"
#include "services/TimeService.h"
```

- [ ] **Step 2: Edit `BaseTheme::drawHeader`**

In `src/components/themes/BaseTheme.cpp::drawHeader` (starts at line 352), add the analogous block at the top — using whatever metrics constants the classic theme uses for its left padding and baseline. The structural pattern matches the Lyra version above.

Add the same two includes.

- [ ] **Step 3: Build**

Run: `pio run -e tiny 2>&1 | tail -5`
Expected: clean.

- [ ] **Step 4: Commit**

```bash
git add src/components/themes/lyra/LyraTheme.cpp src/components/themes/BaseTheme.cpp
git commit -m "feat: draw header clock on Home (Lyra and classic themes)"
```

---

### Task 16: main.cpp — boot wiring

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add include and call**

In `src/main.cpp`, add include near the other service includes:

```cpp
#include "services/TimeService.h"
```

In `setup()` after settings load and after `halGpio.detectDeviceType()` (or wherever the device-type result is in scope), add:

```cpp
TimeService::instance().boot(
    halGpio.deviceType() == HalGPIO::DeviceType::X3
        ? TimeService::DeviceType::X3
        : TimeService::DeviceType::X4);
```

If `halGpio.deviceType()` is named differently in this codebase (check `lib/hal/HalGPIO.h` for the public accessor), use the actual name.

- [ ] **Step 2: Build**

Run: `pio run -e tiny 2>&1 | tail -5`
Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat: boot TimeService at startup"
```

---

### Task 17: Simulator stubs

**Files:**
- Modify: `platformio.ini`
- Create: `crosspoint-simulator/src/HalRtcDS3231Stub.cpp` (or whichever stub mechanism the existing simulator uses) — **only if** the simulator's existing `lib_ignore` strategy isn't sufficient.

The existing simulator pattern: `platformio.ini` for the simulator env lists `hal`, `PNGdec`, `JPEGDEC` in `lib_ignore`, and stubs (e.g. `crosspoint-simulator/src/PNGdec.h`) provide just enough of the API to compile. We do the same thing for the DS3231 backend.

- [ ] **Step 1: Inspect the simulator env section in `platformio.ini`**

Run: `grep -n "simulator\|lib_ignore" platformio.ini`
Note the `[env:simulator]` section. If it has `lib_ignore = …` with `hal`, the entire `lib/hal/` is excluded — meaning `HalRtcInternal` and `HalRtcDS3231` are also excluded. In that case the simulator must already provide `RtcBackend` stubs from the adjacent `crosspoint-simulator` repo.

If `lib/hal/` is NOT entirely excluded but only specific sub-libs are, add `HalRtcDS3231` to `lib_ignore` for the simulator env so the I2C/`Wire` includes don't break the host build.

- [ ] **Step 2: Provide the simulator stub for `HalRtcDS3231`**

If `HalRtcDS3231.cpp` would otherwise be compiled in simulator, exclude it via `lib_ignore` (or, if `lib/hal/` is wholly stubbed in `crosspoint-simulator`, ensure the stub also provides a `HalRtcDS3231` class with `isAvailable() == false`).

For `NtpSyncService`, the simulator path doesn't include `<WiFi.h>` cleanly. Add a `#ifdef CROSSPOINT_SIMULATOR` (or the existing simulator-detect macro the codebase uses; check `platformio.ini` build_flags for the simulator env) block at the top of `NtpSyncService.cpp` that provides a stub implementation:

```cpp
#ifdef CROSSPOINT_SIMULATOR
#include "NtpSyncService.h"
#include "Logging.h"
NtpSyncService& NtpSyncService::instance() { static NtpSyncService s; return s; }
NtpSyncService::Result NtpSyncService::syncOnce(uint32_t, uint32_t) {
  LOG_INF("NTP", "stub: not supported in simulator");
  return { false, 0, Error::WifiConnectFailed };
}
// cancel() / resetCancel() are inline in the header.
#else
// ... existing implementation ...
#endif
```

(If a simulator-specific build flag isn't already defined, add one to the simulator env's `build_flags`: `-DCROSSPOINT_SIMULATOR=1`.)

- [ ] **Step 3: Build the simulator**

Run: `pio run -e simulator 2>&1 | tail -15`
Expected: clean. No errors about `Wire`, `WiFi`, `esp_sntp`, or `HalRtcDS3231`.

- [ ] **Step 4: Manual smoke verify in simulator**

Run: `pio run -e simulator -t upload`
Then in the simulator window:
1. Navigate to Home — header should show the host wall-clock time as `HH:MM`.
2. Settings → Time → Time format → 12h. Return to Home — `H:MM AM/PM`.
3. Settings → Time → Show clock in header → off. Return to Home — left side empty.
4. Settings → Sync time now — popup says "Sync failed" (stub).
5. Settings → Set time manually — enter 03:14, OK. Return to Home — `03:14`.
6. Restart the simulator. Return to Home — shows `03:14` (or close to it; in the host adapter the offset persists).

- [ ] **Step 5: Commit**

```bash
git add platformio.ini src/services/NtpSyncService.cpp
git commit -m "build: simulator stubs for DS3231 and NTP"
```

If the simulator needed actual stub source files in `crosspoint-simulator`, that lives in the adjacent repo per CLAUDE.md and is committed there separately.

---

### Task 18: Web API endpoints

**Files:**
- Modify: `src/network/CrossPointWebServer.cpp`

Three endpoints. Pattern follows the existing handlers in this file (look for one like `server.on("/api/...", HTTP_GET, [](...)`).

- [ ] **Step 1: Add the includes**

Near the top of `CrossPointWebServer.cpp`:

```cpp
#include "services/TimeService.h"
#include "services/NtpSyncService.h"
```

- [ ] **Step 2: Register `GET /api/time`**

Find where other `/api/*` GET handlers are registered. Add:

```cpp
server.on("/api/time", HTTP_GET, [](AsyncWebServerRequest* req) {
  char buf[16];
  bool ok = TimeService::instance().formatLocal(buf, sizeof(buf));
  String body = "{\"local\":\"";
  body += ok ? buf : "";
  body += "\",\"hasTime\":";
  body += ok ? "true" : "false";
  body += "}";
  req->send(200, "application/json", body);
});
```

- [ ] **Step 3: Register `POST /api/time`**

Add:

```cpp
server.on("/api/time", HTTP_POST, [](AsyncWebServerRequest* req) {
  if (!req->hasParam("epochMs", true)) {
    req->send(400, "text/plain", "missing epochMs");
    return;
  }
  String s = req->getParam("epochMs", true)->value();
  long long ms = atoll(s.c_str());
  int64_t epoch = ms / 1000;
  TimeService::instance().onManualSet(epoch);
  req->send(200, "application/json", "{\"ok\":true}");
});
```

- [ ] **Step 4: Register `POST /api/time/sync`**

Add:

```cpp
server.on("/api/time/sync", HTTP_POST, [](AsyncWebServerRequest* req) {
  auto result = NtpSyncService::instance().syncOnce();
  if (result.ok) {
    TimeService::instance().onNtpSynced(result.epoch, /*ignoreManualGuard=*/true);
    String body = "{\"ok\":true,\"epoch\":";
    body += String((long long)result.epoch);
    body += "}";
    req->send(200, "application/json", body);
  } else {
    req->send(503, "application/json", "{\"ok\":false}");
  }
});
```

If the existing handlers use a different request library (look at one to confirm — `AsyncWebServerRequest*` is one common shape; the actual one in this codebase may differ), match the surrounding style exactly.

- [ ] **Step 5: Build**

Run: `pio run -e tiny 2>&1 | tail -5`
Expected: clean.

- [ ] **Step 6: Commit (deferred)** — folded into Task 19.

---

### Task 19: Web UI — SettingsPage.html

**Files:**
- Modify: `src/network/html/SettingsPage.html`

After editing, regenerate via `python scripts/build_html.py`.

- [ ] **Step 1: Find a suitable spot in the HTML**

Open `src/network/html/SettingsPage.html`. Find an existing settings card/section to model from (look for a `<section>` or `<div class="card">` near the top — the file is large; the goal is to match the visual structure).

- [ ] **Step 2: Add the Time section**

Insert (above or below an existing related section):

```html
<section class="card">
  <h2>Time</h2>
  <div class="row">
    <span class="label">Device time</span>
    <span class="value" id="device-time">—</span>
  </div>
  <div class="row">
    <button id="btn-set-browser-time">Set to my browser's time</button>
  </div>
  <div class="row">
    <button id="btn-sync-ntp">Sync via NTP</button>
    <span class="hint" id="sync-result"></span>
  </div>
</section>
<script>
(function() {
  async function refresh() {
    try {
      const r = await fetch('/api/time');
      const j = await r.json();
      document.getElementById('device-time').textContent = j.hasTime ? j.local : '(not set)';
    } catch (e) { /* silent */ }
  }
  document.getElementById('btn-set-browser-time').addEventListener('click', async () => {
    const fd = new FormData();
    fd.append('epochMs', String(Date.now()));
    await fetch('/api/time', { method: 'POST', body: fd });
    refresh();
  });
  document.getElementById('btn-sync-ntp').addEventListener('click', async () => {
    const out = document.getElementById('sync-result');
    out.textContent = 'Syncing…';
    try {
      const r = await fetch('/api/time/sync', { method: 'POST' });
      const j = await r.json();
      out.textContent = j.ok ? ('Synced (epoch ' + j.epoch + ')') : 'Sync failed';
    } catch (e) {
      out.textContent = 'Request failed';
    }
    refresh();
  });
  refresh();
  setInterval(refresh, 5000);
})();
</script>
```

If the existing CSS classes are different (`.card`, `.row`, `.label`, etc.), use whatever is already there. The structure of the JS is what matters.

- [ ] **Step 3: Regenerate the HTML header**

Run: `python scripts/build_html.py`
Expected: `src/network/html/SettingsPageHtml.generated.h` is updated.

- [ ] **Step 4: Build**

Run: `pio run -e tiny 2>&1 | tail -5`
Expected: clean.

- [ ] **Step 5: Commit Tasks 18 + 19**

```bash
git add src/network/CrossPointWebServer.cpp src/network/html/SettingsPage.html src/network/html/SettingsPageHtml.generated.h
git commit -m "feat: web API and UI for time get/set/sync"
```

---

### Task 20: CHANGELOG and final verification

**Files:**
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Add entry under `### Added`**

Insert at the top of the unreleased/upcoming section (matching the existing `Added` style):

```markdown
### Added
- Header clock on the Home screen. Configurable in Settings (24h/12h, UTC offset).
- Cold-boot NTP sync on X4 (uses last-connected Wi-Fi, runs once, then disconnects).
- DS3231 hardware RTC support on X3 (read and write).
- Manual time entry in Settings (on-device and via the web UI).
```

- [ ] **Step 2: Run all host tests one more time**

Run: `bash test/run_time_tests.sh`
Expected: both inner runs pass, final line `All time tests passed.`

- [ ] **Step 3: Build both envs cleanly**

Run: `pio run -e tiny 2>&1 | tail -5` — clean.
Run: `pio run -e simulator 2>&1 | tail -5` — clean.

- [ ] **Step 4: Hardware acceptance checklist (X4)**

Manual; tick each:
- [ ] Cold boot with Wi-Fi: clock appears on Home within ~10s
- [ ] Cold boot without Wi-Fi: header empty; Settings → Sync now → "Sync failed"; Set time → header shows set time
- [ ] Persistence across cold boot: set manual, full power off, power on with Wi-Fi off → header shows last-set
- [ ] Sync-now race: enter manual, immediately Sync now → manual sticks; explicit Sync overrides
- [ ] Web "Set to my browser's time" updates header on next redraw
- [ ] Settings UI: Time category visible; Settings header itself shows no clock (`title != nullptr`)
- [ ] No error spam in serial log during steady state

- [ ] **Step 5: Hardware acceptance checklist (X3)**

Manual; tick each:
- [ ] Cold boot, no Wi-Fi: clock appears immediately (DS3231)
- [ ] Manual set → power-cycle → time persists with no NVS (DS3231 holds it)
- [ ] DS3231 missing/dead: log shows `LOG_ERR("RTC", "DS3231 unavailable, falling back to internal")`; behavior degrades to X4-like

- [ ] **Step 6: Commit**

```bash
git add CHANGELOG.md
git commit -m "docs: changelog entry for header clock"
```

---

## Self-review

**Spec coverage check** (against `docs/superpowers/specs/2026-05-15-clock-display-design.md`):

- §3 decisions all reflected in tasks (NTP cold-boot only — Task 7; DS3231 X3 — Tasks 5/7; fallback persistence — Task 1; 24h default — Tasks 2/8; UTC offset picker — Task 11; redraw-only — Task 15 has no timer; manual entry both — Tasks 13 + 19; X3+X4 — Task 7 selects backend).
- §4 architecture: every named file from §4.2 has a task. ✓
- §4.3 header rendering rule: implemented in Task 15 with the exact structure from the spec. ✓
- §5 settings UI: Task 11 wires the 5 entries; Task 13 is the editor; Task 12 is the popup; Task 19 is the web UI. ✓
- §6 data flow: cold-boot sequence in Task 7 (`boot()`); read path in Task 7 (`formatLocal`) + Task 15 (header); write paths in Task 7 (`onNtpSynced`/`onManualSet`); persistence file format in Task 1 (matches §6.5 byte-for-byte). ✓
- §7 error handling: sanity bounds in Task 2 (`TimeFormat`); CRC/atomic-write in Task 1; `TimeSource::ManuallySet` race guard in Task 7; cancel flag in Task 6 (`NtpSyncService::cancel`). ✓
- §8 testing: Tasks 1, 2, 3 cover Layers 1 and 2; Task 17 covers simulator manual-verify; Task 20 covers Layers 3 and 4. ✓

**Placeholder scan:** no TBDs, no "implement appropriately", no "similar to Task N". The two places where I leaned on the engineer's judgement (the exact `Activity` API method names in Task 12, and the exact CSS class/section structure in Task 19) are flagged inline with explicit "look at the existing pattern and match it" guidance and named files to inspect.

**Type/name consistency:** `formatLocal(char*, size_t)` consistent across Tasks 7, 12, 13, 15, 18. `TimePersistenceStore::Filesystem` consistent across Tasks 1, 7. `TimeSource` enum spelled identically. `SettingAction::SyncTimeNow` / `SetTimeManual` consistent across Tasks 10, 11, 14. `kMinValidEpoch` defined once in `TimeFormat.h` (Task 2), referenced from Tasks 4, 5, 6.

**One known unknown** I deliberately did not pre-resolve: the exact `Activity` base-class method names (`requestRender`, `finish`, `consumeButtonPress`). Tasks 12 and 13 instruct the engineer to peek at `src/activities/Activity.h` and `ButtonRemapActivity.cpp` to match. Hard-coding guesses here would risk being wrong; the structure of the activity is the substance.
