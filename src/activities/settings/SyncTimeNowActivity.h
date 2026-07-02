#pragma once
#include <cstdint>
#include <string>

#include "activities/Activity.h"

// Modal popup that triggers a one-shot NTP sync. Shows
// "Connecting to Wi-Fi..." while the (blocking) sync runs, then briefly
// shows the result before auto-dismissing on success, or waits for
// confirm/back on failure.
class SyncTimeNowActivity final : public Activity {
 public:
  SyncTimeNowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SyncTimeNow", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Phase : uint8_t { Idle, Running, NoCreds, Failed, OkBriefly };

  Phase phase_ = Phase::Idle;
  std::string message_;
  uint32_t okShownAtMs_ = 0;
  bool kicked_ = false;
};
