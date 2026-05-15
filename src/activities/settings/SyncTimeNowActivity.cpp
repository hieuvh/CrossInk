#include "SyncTimeNowActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "services/NtpSyncService.h"
#include "services/TimeService.h"

namespace {
// How long to leave the "Time synced" message on screen before auto-dismiss.
constexpr uint32_t kOkDisplayMs = 1500;
}  // namespace

void SyncTimeNowActivity::onEnter() {
  Activity::onEnter();
  phase_ = Phase::Idle;
  message_ = tr(STR_CONNECTING_WIFI);
  kicked_ = false;
  // Make sure any previously-set cancel flag is cleared before we start.
  NtpSyncService::instance().resetCancel();
  requestUpdate();
}

void SyncTimeNowActivity::onExit() {
  // If the user backed out while a sync was in flight, signal cancel so the
  // service tears down Wi-Fi cleanly. (Safe no-op if nothing was running.)
  NtpSyncService::instance().cancel();
  Activity::onExit();
}

void SyncTimeNowActivity::loop() {
  // Back always exits the popup, cancelling any in-flight sync.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (phase_ == Phase::Running) {
      NtpSyncService::instance().cancel();
    }
    finish();
    return;
  }

  // After OK message is shown briefly, auto-dismiss.
  if (phase_ == Phase::OkBriefly) {
    if (millis() - okShownAtMs_ > kOkDisplayMs) {
      finish();
    }
    return;
  }

  // Error states: wait for Confirm to dismiss. Use wasReleased so the Confirm
  // press that opened this activity doesn't bleed back to SettingsActivity.
  if (phase_ == Phase::NoCreds || phase_ == Phase::Failed) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
    return;
  }

  // First tick: render the "Connecting..." popup, then on the next loop
  // iteration kick off the (blocking) sync. This guarantees the user sees
  // something on screen while the radio comes up.
  if (!kicked_ && phase_ == Phase::Idle) {
    kicked_ = true;
    phase_ = Phase::Running;
    requestUpdate(true);
    return;
  }

  if (phase_ == Phase::Running) {
    auto result = NtpSyncService::instance().syncOnce();
    if (!result.ok) {
      switch (result.error) {
        case NtpSyncService::Error::NoCredentials:
          message_ = tr(STR_NO_SAVED_WIFI);
          phase_ = Phase::NoCreds;
          break;
        default:
          message_ = tr(STR_SYNC_FAILED);
          phase_ = Phase::Failed;
          break;
      }
    } else {
      // ignoreManualGuard=true so an explicit user-initiated sync overrides
      // a previously manually-set time.
      TimeService::instance().onNtpSynced(result.epoch, /*ignoreManualGuard=*/true);
      message_ = tr(STR_SYNC_OK);
      phase_ = Phase::OkBriefly;
      okShownAtMs_ = millis();
    }
    requestUpdate();
  }
}

void SyncTimeNowActivity::render(RenderLock&&) {
  // We deliberately do not clear the screen here: the popup is a modal
  // overlay sitting on top of the previous activity's framebuffer, which
  // is the visual contract for drawPopup() in this codebase.
  GUI.drawPopup(renderer, message_.c_str());
}
