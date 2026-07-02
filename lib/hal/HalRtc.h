#pragma once
#include <cstdint>

// Sanity bounds for any "is the wall-clock time real?" check.
// kMinValidEpoch is 2025-01-01T00:00:00Z (any earlier value almost certainly
// means the RTC has been reset and is reporting an uninitialized default).
// kMaxValidEpoch is 2100-01-01T00:00:00Z (firmware lifetime guard).
// Canonical home for these constants — RtcBackend backends and TimeFormat
// both reference them.
constexpr int64_t kMinValidEpoch = 1735689600;   // 2025-01-01T00:00:00Z
constexpr int64_t kMaxValidEpoch = 4102444800;   // 2100-01-01T00:00:00Z

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
