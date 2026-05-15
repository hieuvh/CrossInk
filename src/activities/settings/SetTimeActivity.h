#pragma once
#include <cstdint>

#include "activities/Activity.h"

// On-device HH:MM editor. Up/Down adjusts the selected field; Left/Right
// switches field; Confirm saves and exits; Back cancels.
class SetTimeActivity final : public Activity {
 public:
  SetTimeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SetTime", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // 0 = hours, 1 = minutes
  uint8_t field_ = 0;
  int hour_ = 0;
  int minute_ = 0;

  // UTC seconds at 00:00:00 of whatever date the RTC currently believes
  // (or a sane fallback). We re-derive the new epoch by adding the user's
  // chosen hours/minutes (interpreted as local time) and reversing the
  // configured UTC offset.
  int64_t dateGuessAtMidnightUtc_ = 0;

  void seedFromCurrentTime();
  int64_t composeUtcEpoch() const;
};
