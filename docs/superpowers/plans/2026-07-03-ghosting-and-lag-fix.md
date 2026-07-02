# Ghosting Prevention and Settings Lag Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate Settings screen navigation lag and automatically prevent E-ink display ghosting/burn-in system-wide.

**Architecture:** 
1. Optimize [SettingsActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/settings/SettingsActivity.cpp) to run `renderUIAntiAliased` only on page-level transitions, bypassing it during incremental selection changes.
2. Implement consecutive fast refresh tracking and automatic full-refresh promotion inside [HalDisplay.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.cpp).

**Tech Stack:** C++, ESP-IDF / Arduino-ESP32, E-Ink Display Driver.

## Global Constraints
- Target is ESP32-C3 with tight memory constraints. Keep local stack allocations minimal.
- Do not introduce CPU/heap churn inside high-frequency loop calls.
- Adhere to HAL boundaries; all physical screen interactions must flow through the `HalDisplay` layer.

---

### Task 1: Optimize Settings Render Path

**Files:**
- Modify: `src/activities/settings/SettingsActivity.cpp`

**Interfaces:**
- Consumes: None
- Produces: Optimized `SettingsActivity::render` logic.

- [ ] **Step 1: Check the SettingsActivity render logic**
  Locate where `UIRenderUtils::renderUIAntiAliased` is called (lines 518-520).
- [ ] **Step 2: Limit the anti-aliasing call to non-windowed updates**
  Modify the `render` function to only run `renderUIAntiAliased` in the `else` branch of `useWindowedUpdate`.
- [ ] **Step 3: Verify simulator compilation**
  Run `pio run -e simulator` to make sure it compiles.
- [ ] **Step 4: Commit**
  Commit the change with: `git commit -am "perf: limit settings anti-aliasing to page transitions to eliminate selection lag"`

---

### Task 2: Implement Automatic Refresh Promotion in `HalDisplay`

**Files:**
- Modify: `lib/hal/HalDisplay.h`
- Modify: `lib/hal/HalDisplay.cpp`

**Interfaces:**
- Consumes: None
- Produces: Self-cleaning display updates via promoted E-ink waveforms.

- [ ] **Step 1: Declare the fast refresh counter in header**
  Modify [HalDisplay.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.h) to add `int consecutiveFastRefreshes = 0;` as a private member of `HalDisplay`.
- [ ] **Step 2: Initialize the counter in constructor**
  Modify [HalDisplay.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.cpp) to initialize `consecutiveFastRefreshes(0)`.
- [ ] **Step 3: Update `displayBuffer` to track and promote fast refreshes**
  Inside `HalDisplay::displayBuffer`, check if the refresh mode is `FAST_REFRESH`. If so, increment the counter. If the counter reaches `8`, override the mode to `HALF_REFRESH` and reset the counter. If the mode is already `HALF_REFRESH` or `FULL_REFRESH`, reset the counter to `0`.
- [ ] **Step 4: Update `displayWindow` to track and promote fast refreshes**
  Inside `HalDisplay::displayWindow`, increment the counter. If the counter reaches `8`, reset the counter and trigger a full screen `displayBuffer(HALF_REFRESH)` to clean the screen. Otherwise, proceed with `displayWindow`.
- [ ] **Step 5: Reset counter on grayscale buffer display**
  Inside `HalDisplay::displayGrayBuffer()`, reset the counter to `0`.
- [ ] **Step 6: Verify simulator compilation**
  Run `pio run -e simulator` to verify.
- [ ] **Step 7: Verify customink compilation**
  Run `pio run -e customink` to verify the target device builds cleanly.
- [ ] **Step 8: Commit**
  Commit the changes with: `git commit -am "feat: implement automatic full-refresh promotion in HalDisplay to prevent burn-in"`
