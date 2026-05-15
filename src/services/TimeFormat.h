#pragma once
#include <cstddef>
#include <cstdint>

// Sanity bounds for epoch validity live in the HAL header alongside RtcBackend
// (the canonical home for "is this clock real?" checks). Re-exported here so
// existing call sites that read `TimeFormat::kMinValidEpoch` keep working.
#include "../../lib/hal/HalRtc.h"

namespace TimeFormat {

constexpr int64_t kMinValidEpoch = ::kMinValidEpoch;
constexpr int64_t kMaxValidEpoch = ::kMaxValidEpoch;

// Returns true on success and writes a NUL-terminated string to `out`.
// `format24h=true` → "HH:MM" (5 chars). `format24h=false` → "H:MM AM/PM" (up to 8 chars).
// Returns false if `epoch` is outside the sanity bounds, or if the buffer is too small.
bool format(int64_t utcEpoch, int offsetHours, bool format24h, char* out, size_t cap);

}  // namespace TimeFormat
