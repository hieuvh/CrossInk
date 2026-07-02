# Sharper Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve visual quality across text, images, and UI on both X4 (800x480) and X3 (792x528) e-ink panels, while staying safely within the ESP32-C3's ~380 KB RAM budget.

**Architecture:** We will eliminate the memory-intensive 48 KB `storeBwBuffer()` by re-rendering BW content after the grayscale pass, use device-adaptive dithering profiles, add horizontal image sharpening during decode, introduce UI anti-aliasing via post-refresh re-rendering, and use partial windowed updates for Settings scrolling.

**Tech Stack:** C++, ESP-IDF / Arduino-ESP32, GfxRenderer.

## Global Constraints
- Target hardware: X4 (800x480) and X3 (792x528) e-ink displays.
- Peak memory constraint: Stay within 380 KB usable RAM (no heap leaks).
- PlatformIO simulator environment: `simulator` (compile validation).
- Standard logging: Use `LOG_INF`, `LOG_DBG`, and `LOG_ERR`.

---

### Task 1: Refactor Reader AA to Eliminate `storeBwBuffer()`

**Files:**
- Modify: [ReaderUtils.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/reader/ReaderUtils.h)
- Modify: [EpubReaderActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/reader/EpubReaderActivity.cpp)
- Modify: [TxtReaderActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/reader/TxtReaderActivity.cpp)

**Interfaces:**
- Consumes: `GfxRenderer::clearScreen`, `GfxRenderer::setRenderMode`, `GfxRenderer::copyGrayscaleLsbBuffers`, `GfxRenderer::copyGrayscaleMsbBuffers`, `GfxRenderer::displayGrayBuffer`, `GfxRenderer::cleanupGrayscaleWithFrameBuffer`.
- Produces: Grayscale AA rendering inside readers that uses re-rendering instead of allocating the 48 KB store buffer.

- [ ] **Step 1: Modify `ReaderUtils.h` renderAntiAliased template**
  Update the template definition of `renderAntiAliased` to avoid `storeBwBuffer()` and `restoreBwBuffer()` by using a re-render pass:
  ```cpp
  template <typename RenderFn, typename AbortFn>
  void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn, AbortFn&& shouldAbort) {
    if (shouldAbort()) {
      return;
    }

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderFn();
    renderer.copyGrayscaleLsbBuffers();

    if (shouldAbort()) {
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.clearScreen();
      renderFn();
      renderer.cleanupGrayscaleWithFrameBuffer();
      return;
    }

    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderFn();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    // Re-render BW to restore framebuffer state
    renderer.clearScreen();
    renderFn();
    renderer.cleanupGrayscaleWithFrameBuffer();
  }
  ```

- [ ] **Step 2: Modify `EpubReaderActivity.cpp` grayscale section**
  Find the grayscale block inside `EpubReaderActivity.cpp` around line 1648. Remove `renderer.storeBwBuffer()` and `renderer.restoreBwBuffer()`. Change it to clear and re-render the page:
  ```cpp
  // Replace:
  // const bool storedBwBuffer = renderer.storeBwBuffer();
  // With:
  const bool canApplyGrayscale = needsAnyGrayscale;

  if (canApplyGrayscale) {
    if (activityManager.hasPendingRender()) {
      renderer.clearScreen();
      renderer.setRenderMode(GfxRenderer::BW);
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
      renderer.cleanupGrayscaleWithFrameBuffer();
      return;
    }

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    if (needsTextGrayscale) {
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    } else {
      page->renderImages(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    }
    renderer.copyGrayscaleLsbBuffers();

    if (activityManager.hasPendingRender()) {
      renderer.clearScreen();
      renderer.setRenderMode(GfxRenderer::BW);
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
      renderer.cleanupGrayscaleWithFrameBuffer();
      return;
    }

    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    if (needsTextGrayscale) {
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    } else {
      page->renderImages(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    }
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    renderer.clearScreen();
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
    renderer.cleanupGrayscaleWithFrameBuffer();
  }
  ```

- [ ] **Step 3: Modify `TxtReaderActivity.cpp` grayscale section**
  Find where TXT reader calls `renderAntiAliased()` (around line 421) and make sure it passes the correct arguments, or update `TxtReaderActivity.cpp` to use the re-render pipeline.

- [ ] **Step 4: Verify build compiles**
  Run: `pio run -e simulator`
  Expected: Success.

- [ ] **Step 5: Commit changes**
  ```bash
  git add src/activities/reader/ReaderUtils.h src/activities/reader/EpubReaderActivity.cpp src/activities/reader/TxtReaderActivity.cpp
  git commit -m "refactor: eliminate storeBwBuffer to reclaim 48KB heap"
  ```

---

### Task 2: Implement Device-Adaptive Dithering Profiles

**Files:**
- Modify: [BitmapHelpers.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/BitmapHelpers.h)
- Modify: [BitmapHelpers.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/BitmapHelpers.cpp)
- Modify: [Bitmap.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/Bitmap.cpp)
- Modify: [JpegToBmpConverter.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/JpegToBmpConverter/JpegToBmpConverter.cpp)

**Interfaces:**
- Consumes: `gpio.deviceIsX3()`.
- Produces: `DitherProfile` structure, parameterized `AtkinsonDitherer` and `FloydSteinbergDitherer`.

- [ ] **Step 1: Define `DitherProfile` in `BitmapHelpers.h`**
  Add the following definition:
  ```cpp
  struct DitherProfile {
    int thresholds[3];
    int quantizedValues[4];
  };

  extern const DitherProfile kDitherProfileX4;
  extern const DitherProfile kDitherProfileX3;
  ```

- [ ] **Step 2: Update ditherers in `BitmapHelpers.h` to accept profile**
  Update `AtkinsonDitherer` constructor:
  ```cpp
  class AtkinsonDitherer {
   public:
    explicit AtkinsonDitherer(int width, const DitherProfile& profile = kDitherProfileX4)
        : width(width), profile(profile) { ... }
   ...
   private:
    int width;
    const DitherProfile& profile;
    ...
  };
  ```
  Replace hardcoded thresholds inside `AtkinsonDitherer::processPixel()` and `FloydSteinbergDitherer::processPixel()` with lookups using the `profile`:
  ```cpp
  // For AtkinsonDitherer::processPixel
  uint8_t quantized;
  int quantizedValue;
  if (adjusted < profile.thresholds[0]) {
    quantized = 0;
    quantizedValue = profile.quantizedValues[0];
  } else if (adjusted < profile.thresholds[1]) {
    quantized = 1;
    quantizedValue = profile.quantizedValues[1];
  } else if (adjusted < profile.thresholds[2]) {
    quantized = 2;
    quantizedValue = profile.quantizedValues[2];
  } else {
    quantized = 3;
    quantizedValue = profile.quantizedValues[3];
  }
  ```

- [ ] **Step 3: Define profiles in `BitmapHelpers.cpp`**
  ```cpp
  const DitherProfile kDitherProfileX4 = {
    {30, 50, 140},
    {15, 30, 80, 210}
  };

  const DitherProfile kDitherProfileX3 = {
    {40, 80, 160},
    {20, 55, 120, 220}
  };

  // Helper function to resolve profile at runtime
  const DitherProfile& getDeviceDitherProfile() {
    #ifdef SIMULATOR
    return kDitherProfileX4;
    #else
    return gpio.deviceIsX3() ? kDitherProfileX3 : kDitherProfileX4;
    #endif
  }
  ```

- [ ] **Step 4: Update ditherer construction in `Bitmap.cpp` and JPEG/PNG decoders**
  Instantiate `AtkinsonDitherer` and `FloydSteinbergDitherer` passing `getDeviceDitherProfile()`.

- [ ] **Step 5: Verify build compiles**
  Run: `pio run -e simulator`
  Expected: Success.

- [ ] **Step 6: Commit changes**
  ```bash
  git add lib/GfxRenderer/BitmapHelpers.h lib/GfxRenderer/BitmapHelpers.cpp lib/GfxRenderer/Bitmap.cpp lib/JpegToBmpConverter/JpegToBmpConverter.cpp
  git commit -m "feat: add device-adaptive dithering profiles for X3 and X4"
  ```

---

### Task 3: Add Image Sharpening Pre-Pass

**Files:**
- Modify: [BitmapHelpers.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/BitmapHelpers.h)
- Modify: [JpegToBmpConverter.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/JpegToBmpConverter/JpegToBmpConverter.cpp)
- Modify: [CrossPointSettings.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/CrossPointSettings.h)
- Modify: [CrossPointSettings.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/CrossPointSettings.cpp)
- Modify: [SettingsList.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/SettingsList.h)

**Interfaces:**
- Consumes: `SETTINGS.imageSharpening`.
- Produces: In-place row sharpening function.

- [ ] **Step 1: Add sharpening helper in `BitmapHelpers.h`**
  ```cpp
  inline void sharpenRow(uint8_t* row, int width, float amount) {
    if (amount <= 0.0f || width < 3) return;
    
    // We do in-place sharpening using a temp copy of the row
    std::vector<uint8_t> temp(row, row + width);
    for (int x = 1; x < width - 1; x++) {
      int center = temp[x];
      int neighbors = (temp[x - 1] + temp[x + 1]) / 2;
      int sharpened = center + static_cast<int>(amount * (center - neighbors));
      row[x] = static_cast<uint8_t>(std::clamp(sharpened, 0, 255));
    }
  }
  ```

- [ ] **Step 2: Apply sharpening inside `JpegToBmpConverter.cpp`**
  Add call to `sharpenRow` right before pixels are passed to the ditherer. Use float value based on settings:
  - 0 (off) = 0.0f
  - 1 (subtle) = 0.3f
  - 2 (strong) = 0.6f

- [ ] **Step 3: Add `imageSharpening` to Settings**
  In `CrossPointSettings.h`, add:
  ```cpp
  uint8_t imageSharpening = 1; // 0=off, 1=subtle, 2=strong
  ```
  In `CrossPointSettings.cpp`, serialize/deserialize `imageSharpening`.
  In `SettingsList.h`, register `imageSharpening` toggle or selection list.

- [ ] **Step 4: Verify build compiles**
  Run: `pio run -e simulator`
  Expected: Success.

- [ ] **Step 5: Commit changes**
  ```bash
  git add lib/GfxRenderer/BitmapHelpers.h lib/JpegToBmpConverter/JpegToBmpConverter.cpp src/CrossPointSettings.h src/CrossPointSettings.cpp src/SettingsList.h
  git commit -m "feat: add image sharpening pre-pass setting and row helper"
  ```

---

### Task 4: Add UI Anti-Aliasing (Post-Refresh)

**Files:**
- Create: `src/activities/util/UIRenderUtils.h`
- Modify: [HomeActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/home/HomeActivity.cpp)
- Modify: [SettingsActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/settings/SettingsActivity.cpp)

**Interfaces:**
- Consumes: `SETTINGS.textAntiAliasing`.
- Produces: Grayscale UI rendering overlay.

- [ ] **Step 1: Create `src/activities/util/UIRenderUtils.h`**
  Create the file and write `UIRenderUtils::renderUIAntiAliased` helper:
  ```cpp
  #pragma once
  #include <GfxRenderer.h>

  namespace UIRenderUtils {

  template <typename RenderFn>
  void renderUIAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderFn();
    renderer.copyGrayscaleLsbBuffers();

    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderFn();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    renderer.clearScreen();
    renderFn();
    renderer.cleanupGrayscaleWithFrameBuffer();
  }

  } // namespace UIRenderUtils
  ```

- [ ] **Step 2: Add post-AA pass to `HomeActivity.cpp`**
  Include `UIRenderUtils.h`. After calls to `renderer.displayBuffer()` (in slow path render flow), if `SETTINGS.textAntiAliasing` is enabled, call `UIRenderUtils::renderUIAntiAliased()`.

- [ ] **Step 3: Add post-AA pass to `SettingsActivity.cpp`**
  Include `UIRenderUtils.h`. In `SettingsActivity::render`, after `renderer.displayBuffer()`, if `SETTINGS.textAntiAliasing` is enabled, call `UIRenderUtils::renderUIAntiAliased()` passing the render callback.

- [ ] **Step 4: Verify build compiles**
  Run: `pio run -e simulator`
  Expected: Success.

- [ ] **Step 5: Commit changes**
  ```bash
  git add src/activities/util/UIRenderUtils.h src/activities/home/HomeActivity.cpp src/activities/settings/SettingsActivity.cpp
  git commit -m "feat: add post-refresh anti-aliasing to Home and Settings UI"
  ```

---

### Task 5: Implement Windowed Partial Updates for Settings (X4 Only)

**Files:**
- Modify: [GfxRenderer.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/GfxRenderer.h)
- Modify: [GfxRenderer.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/GfxRenderer.cpp)
- Modify: [HalDisplay.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.h)
- Modify: [HalDisplay.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.cpp)
- Modify: [SettingsActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/settings/SettingsActivity.cpp)

**Interfaces:**
- Consumes: `EInkDisplay::displayWindow`.
- Produces: Windowed screen update wrapper.

- [ ] **Step 1: Expose `displayWindow` in GfxRenderer**
  Uncomment the `displayWindow()` method declaration in `GfxRenderer.h`:
  ```cpp
  void displayWindow(int x, int y, int width, int height) const;
  ```
  Implement the method in `GfxRenderer.cpp` map coordinates orientation-awarely and call `display.displayWindow`.

- [ ] **Step 2: Expose `displayWindow` in HalDisplay**
  Add declaration in `HalDisplay.h`:
  ```cpp
  void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool turnOffScreen = false);
  ```
  Implement in `HalDisplay.cpp`:
  ```cpp
  void HalDisplay::displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool turnOffScreen) {
    HalSpiBus::Lock spiLock;
    einkDisplay.displayWindow(x, y, w, h, turnOffScreen);
  }
  ```

- [ ] **Step 3: Modify `SettingsActivity.cpp` to use windowed updates**
  Track the previously selected index. When the selection changes incrementally (e.g. `prevIndex != -1` and `abs(selectedIndex - prevIndex) == 1`), calculate the vertical bounds of the old row and the new row.
  Use `renderer.displayWindow()` for that bounding box instead of `renderer.displayBuffer()`.
  Fallback to full display buffer on page scroll or big jumps.

- [ ] **Step 4: Verify build compiles**
  Run: `pio run -e simulator`
  Expected: Success.

- [ ] **Step 5: Commit changes**
  ```bash
  git add lib/GfxRenderer/GfxRenderer.h lib/GfxRenderer/GfxRenderer.cpp lib/hal/HalDisplay.h lib/hal/HalDisplay.cpp src/activities/settings/SettingsActivity.cpp
  git commit -m "feat: implement experimental windowed updates for list selection on X4"
  ```
