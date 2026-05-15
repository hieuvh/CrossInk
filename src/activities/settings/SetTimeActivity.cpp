#include "SetTimeActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "services/TimeService.h"

namespace {
// 2026-01-01T00:00:00Z, used when the RTC has no valid time yet.
constexpr int64_t kFallbackDateMidnightUtc = 1767225600;

constexpr int kSecondsPerDay = 86400;

int64_t midnightOfDateUtc(int64_t epoch) { return epoch - (epoch % kSecondsPerDay); }

// Field selector constants for readability.
constexpr uint8_t kFieldHours = 0;
constexpr uint8_t kFieldMinutes = 1;
}  // namespace

void SetTimeActivity::onEnter() {
  Activity::onEnter();
  field_ = kFieldHours;
  seedFromCurrentTime();
  requestUpdate();
}

void SetTimeActivity::onExit() { Activity::onExit(); }

void SetTimeActivity::seedFromCurrentTime() {
  const int offsetHours = static_cast<int>(SETTINGS.utcOffsetIndex) - 12;

  int64_t utcEpoch = 0;
  // Read through TimeService so we hit the active RtcBackend (DS3231 on X3,
  // internal RTC on X4, SimRtcBackend in simulator). gettimeofday() would
  // return the C3 internal RTC on X3, which is not kept in sync with DS3231.
  if (TimeService::instance().getCurrentUtcEpoch(&utcEpoch)) {
    const int64_t localEpoch = utcEpoch + static_cast<int64_t>(offsetHours) * 3600;
    const int64_t secondsInDay = ((localEpoch % kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay;
    hour_ = static_cast<int>(secondsInDay / 3600);
    minute_ = static_cast<int>((secondsInDay % 3600) / 60);
    // Use the (UTC) date that contains this local time. We round the local
    // time to its midnight, then convert back to a UTC anchor by undoing
    // the offset; midnightOfDateUtc keeps the math in seconds-since-epoch.
    const int64_t localMidnight = localEpoch - secondsInDay;
    dateGuessAtMidnightUtc_ = localMidnight - static_cast<int64_t>(offsetHours) * 3600;
    return;
  }

  // No valid RTC: start the editor at 00:00 on the fallback date.
  hour_ = 0;
  minute_ = 0;
  dateGuessAtMidnightUtc_ = kFallbackDateMidnightUtc;
}

int64_t SetTimeActivity::composeUtcEpoch() const {
  const int offsetHours = static_cast<int>(SETTINGS.utcOffsetIndex) - 12;
  const int64_t local =
      dateGuessAtMidnightUtc_ + static_cast<int64_t>(offsetHours) * 3600 +
      static_cast<int64_t>(hour_) * 3600 + static_cast<int64_t>(minute_) * 60;
  return local - static_cast<int64_t>(offsetHours) * 3600;
}

void SetTimeActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    field_ = kFieldHours;
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    field_ = kFieldMinutes;
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (field_ == kFieldHours) {
      hour_ = (hour_ + 1) % 24;
    } else {
      minute_ = (minute_ + 1) % 60;
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (field_ == kFieldHours) {
      hour_ = (hour_ + 23) % 24;
    } else {
      minute_ = (minute_ + 59) % 60;
    }
    requestUpdate();
    return;
  }
  // Use wasReleased so the Confirm press that opened this activity doesn't
  // bleed back to SettingsActivity (which uses wasReleased for Confirm) and
  // immediately re-open us. Matches the StatusBarSettingsActivity pattern.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    TimeService::instance().onManualSet(composeUtcEpoch());
    finish();
    return;
  }
}

void SetTimeActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SET_TIME_MANUAL));

  char hh[8];
  std::snprintf(hh, sizeof(hh), "%02d", hour_);
  char mm[8];
  std::snprintf(mm, sizeof(mm), "%02d", minute_);

  // Layout the "HH : MM" group centred horizontally and vertically.
  constexpr int kGap = 12;     // gap between digits and colon
  constexpr int kPadX = 6;     // horizontal padding inside the field highlight
  constexpr int kPadY = 8;     // vertical padding inside the field highlight
  constexpr int kFieldHeight = 36;

  const int hwid = renderer.getTextWidth(UI_12_FONT_ID, hh, EpdFontFamily::BOLD);
  const int cwid = renderer.getTextWidth(UI_12_FONT_ID, ":", EpdFontFamily::BOLD);
  const int mwid = renderer.getTextWidth(UI_12_FONT_ID, mm, EpdFontFamily::BOLD);
  const int total = hwid + cwid + mwid + kGap * 2;

  int x = (pageWidth - total) / 2;
  const int y = pageHeight / 2;

  if (field_ == kFieldHours) {
    renderer.fillRect(x - kPadX, y - kPadY, hwid + kPadX * 2, kFieldHeight, true);
    renderer.drawText(UI_12_FONT_ID, x, y, hh, false, EpdFontFamily::BOLD);
  } else {
    renderer.drawText(UI_12_FONT_ID, x, y, hh, true, EpdFontFamily::BOLD);
  }
  x += hwid + kGap;
  renderer.drawText(UI_12_FONT_ID, x, y, ":", true, EpdFontFamily::BOLD);
  x += cwid + kGap;
  if (field_ == kFieldMinutes) {
    renderer.fillRect(x - kPadX, y - kPadY, mwid + kPadX * 2, kFieldHeight, true);
    renderer.drawText(UI_12_FONT_ID, x, y, mm, false, EpdFontFamily::BOLD);
  } else {
    renderer.drawText(UI_12_FONT_ID, x, y, mm, true, EpdFontFamily::BOLD);
  }

  // Standard button hints row at the bottom of the screen.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
