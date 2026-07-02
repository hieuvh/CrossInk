# Design Spec: Ghosting Prevention and Settings Lag Fix

This specification outlines the architecture and implementation details for solving E-ink display burn-in (ghosting) and Settings menu selection navigation lag.

## Problem Description
1. **Settings Menu Navigation Lag:** When moving the settings selection up or down within the same page, the display performs a windowed fast update (`displayWindow`). However, if `textAntiAliasing` is enabled, the code immediately calls `UIRenderUtils::renderUIAntiAliased`. This helper triggers a full-screen anti-aliasing pass which executes a full-screen grayscale update (`displayGrayBuffer`) on *every single selection change*. This negates the benefit of partial windowed updates and causes severe lags.
2. **Burn-in and Ghosting (Image Overlapping):** Repeating fast/partial refreshes (`FAST_REFRESH` and `displayWindow`) on E-ink screens causes physical charge accumulation, leading to visible ghosting and overlapping artifacts. A full waveform clearing refresh is required periodically.

## Proposed Changes

### 1. Optimize Settings Render Pipeline
In [SettingsActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/settings/SettingsActivity.cpp), we will restrict the text anti-aliasing pass (`renderUIAntiAliased`) to run only during full screen changes (e.g., category transitions or page scrolling). Incremental row selection changes will skip the anti-aliasing pass, yielding instant, lag-free transitions.

### 2. Automatic Full Refresh Promotion in `HalDisplay`
We will implement consecutive fast refresh tracking inside [HalDisplay](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.h) to automatically clean the screen after 8 consecutive fast/windowed updates.

---

### [hal] Component

#### [MODIFY] [HalDisplay.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.h)
- Add private member `int consecutiveFastRefreshes = 0;` to track the number of consecutive partial updates.

#### [MODIFY] [HalDisplay.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.cpp)
- Initialize `consecutiveFastRefreshes` to `0` in the constructor.
- Update `displayBuffer(mode, turnOffScreen)`:
  - If `mode == FAST_REFRESH`, increment `consecutiveFastRefreshes`.
  - If `consecutiveFastRefreshes >= 8`, change `mode` to `HALF_REFRESH` and reset the counter to `0`.
  - If `mode` is `HALF_REFRESH` or `FULL_REFRESH`, reset the counter to `0`.
- Update `displayWindow(x, y, w, h, turnOffScreen)`:
  - Increment `consecutiveFastRefreshes`.
  - If `consecutiveFastRefreshes >= 8`, promote to a full-screen `HALF_REFRESH` update to completely clear the screen of ghosting, and reset the counter.
  - Otherwise, perform the standard `einkDisplay.displayWindow` update.
- Update `displayGrayBuffer()`:
  - Reset `consecutiveFastRefreshes = 0`.

---

### [activities] Component

#### [MODIFY] [SettingsActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/settings/SettingsActivity.cpp)
- Change lines 512-520 so that `renderUIAntiAliased` is only called if `useWindowedUpdate` is false:
```cpp
  if (useWindowedUpdate) {
    renderer.displayWindow(0, yMin, pageWidth, yHeight);
  } else {
    renderer.displayBuffer();
    if (SETTINGS.textAntiAliasing) {
      UIRenderUtils::renderUIAntiAliased(renderer, drawSettingsContent);
    }
  }
```

## Verification Plan

### Automated Verification
- Verify that the code compiles cleanly for the ESP32 hardware build environment:
  `pio run -e customink`
- Verify that the simulator compiles cleanly:
  `pio run -e simulator`

### Manual Verification
- Deploy the build to the device and verify that scrolling through items in the Settings menu is instantaneous and lag-free.
- Verify that after navigating the settings menu or the home screen (moving selection 8 times), a full clear refresh is automatically triggered to clear any accumulated ghosting or burn-in.
