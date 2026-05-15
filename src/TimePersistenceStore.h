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
