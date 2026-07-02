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
