#pragma once
#include "HalRtc.h"

// Internal-RTC backend. On ESP32 newlib's gettimeofday/settimeofday talk
// directly to the chip's internal RTC, which loses time on hard power loss
// but survives deep sleep. This is the X4 backend (no DS3231 onboard).
class HalRtcInternal : public RtcBackend {
 public:
  bool readUtcEpoch(int64_t* out) override;
  bool writeUtcEpoch(int64_t utcEpoch) override;
  bool hasValidTime() override;
};
