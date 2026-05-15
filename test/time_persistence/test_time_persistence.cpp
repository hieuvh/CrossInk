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
