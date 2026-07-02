#include "TimeFormat.h"
#include <cstdio>

namespace TimeFormat {

bool format(int64_t utcEpoch, int offsetHours, bool format24h, char* out, size_t cap) {
  if (utcEpoch < kMinValidEpoch || utcEpoch > kMaxValidEpoch) return false;
  if (out == nullptr || cap == 0) return false;

  const int64_t local = utcEpoch + int64_t(offsetHours) * 3600;
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
