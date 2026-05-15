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
