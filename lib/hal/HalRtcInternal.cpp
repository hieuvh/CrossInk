#include "HalRtcInternal.h"

#include <sys/time.h>

bool HalRtcInternal::readUtcEpoch(int64_t* out) {
  if (out == nullptr) return false;
  timeval tv;
  if (gettimeofday(&tv, nullptr) != 0) return false;
  *out = int64_t(tv.tv_sec);
  return *out >= kMinValidEpoch;
}

bool HalRtcInternal::writeUtcEpoch(int64_t utcEpoch) {
  timeval tv{ time_t(utcEpoch), 0 };
  return settimeofday(&tv, nullptr) == 0;
}

bool HalRtcInternal::hasValidTime() {
  int64_t epoch;
  return readUtcEpoch(&epoch);
}
