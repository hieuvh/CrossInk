#include "TimePersistenceStore.h"
#include <cstring>

namespace {
// On-disk layout for /.crosspoint/time.bin. The file is written byte-for-byte
// from this struct in host endianness. Both x86_64/arm64 hosts and the ESP32-C3
// target are little-endian, so the file is portable in practice.
//
// The struct is 24 bytes total: bytes 20..23 are trailing padding kept zero by
// value-initialization (Payload{}), and CRC coverage stops at byte 16 via
// kPayloadCoreSize so the trailing padding never affects the checksum.
struct Payload {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  int64_t  lastSyncedUtc;
  uint32_t crc32;
};
static_assert(sizeof(Payload) == 24, "Payload size must remain 24 bytes for on-disk compatibility");
static_assert(offsetof(Payload, crc32) == 16, "crc32 must sit at offset 16 (no padding before it)");
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
