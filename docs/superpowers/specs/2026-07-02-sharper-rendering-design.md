# Sharper Rendering Without Crashes

Improve visual quality across text, images, and UI on both X4 (800×480) and X3 (792×528) e-ink panels, while staying safely within the ESP32-C3's ~380 KB RAM budget.

## Summary

Five coordinated changes to the rendering pipeline:

1. **Eliminate `storeBwBuffer()`** — Reclaim 48 KB peak heap by re-rendering BW content after grayscale display (EPUB/TXT readers)
2. **Device-adaptive dithering** — Separate quantization profiles for X4 and X3 panels
3. **Image sharpening pre-pass** — Row-level unsharp mask during JPEG/PNG decode, zero extra buffers
4. **UI anti-aliasing** — Extend the grayscale AA pass to Home, Settings, and File Browser screens
5. **Windowed partial updates** — Use `displayWindow()` for list selection changes (X4 only, experimental)

## Section 1: Eliminate `storeBwBuffer()` in EPUB & TXT Readers

### Problem

The current grayscale anti-aliasing pipeline stores a full backup of the BW framebuffer before overwriting it with grayscale data. This costs **48 KB at peak** (6 × 8 KB chunks via `malloc`), making it the single largest transient allocation in the system.

### Current Flow

```
storeBwBuffer()     → malloc 6 × 8KB chunks = 48KB
clearScreen(0x00)
LSB pass: render → copyGrayscaleLsbBuffers()
MSB pass: render → copyGrayscaleMsbBuffers()
displayGrayBuffer()
restoreBwBuffer()   → memcpy back, free 48KB
```

Source: [EpubReaderActivity.cpp:1648–1704](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/reader/EpubReaderActivity.cpp#L1648-L1704)

### New Flow

Modeled on the XTC reader's proven approach ([XtcReaderActivity.cpp:300–362](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/reader/XtcReaderActivity.cpp#L300-L362)):

```
clearScreen(0x00)
LSB pass: render → copyGrayscaleLsbBuffers()
MSB pass: render → copyGrayscaleMsbBuffers()
displayGrayBuffer()
clearScreen()                       ← white fill
Re-render BW content to framebuffer ← ~50-150ms CPU
cleanupGrayscaleWithFrameBuffer()   ← sync RED RAM from re-rendered BW
```

### Files Changed

#### [MODIFY] [EpubReaderActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/reader/EpubReaderActivity.cpp)

Replace the `storeBwBuffer()`/`restoreBwBuffer()` block (lines ~1648–1718) with the re-render approach. The re-render callback is the same `page->render()` call already used for LSB/MSB passes. The early-abort checks (`activityManager.hasPendingRender()`) remain — they just skip the re-render instead of calling `restoreBwBuffer()`.

Key change: after `displayGrayBuffer()`, instead of `restoreBwBuffer()`:
```cpp
renderer.clearScreen();
renderer.setRenderMode(GfxRenderer::BW);
page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
renderer.cleanupGrayscaleWithFrameBuffer();
```

#### [MODIFY] [TxtReaderActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/reader/TxtReaderActivity.cpp)

Same pattern as EPUB. The TXT reader's grayscale block (around line 421) uses `ReaderUtils::renderAntiAliased()` — update the template.

#### [MODIFY] [ReaderUtils.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/reader/ReaderUtils.h)

Update the `renderAntiAliased()` template to use re-render instead of store/restore:

```cpp
template <typename RenderFn, typename AbortFn>
void renderAntiAliased(GfxRenderer& renderer, RenderFn&& renderFn, AbortFn&& shouldAbort) {
  if (shouldAbort()) return;

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderFn();
  renderer.copyGrayscaleLsbBuffers();

  if (shouldAbort()) {
    renderer.setRenderMode(GfxRenderer::BW);
    // Re-render BW to restore framebuffer state
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

  // Re-render BW content to restore framebuffer (replaces storeBwBuffer/restoreBwBuffer)
  renderer.clearScreen();
  renderFn();
  renderer.cleanupGrayscaleWithFrameBuffer();
}
```

#### [NO CHANGE] [SleepActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/boot_sleep/SleepActivity.cpp)

Keep `storeBwBuffer()` in the sleep screen — it renders once, nothing else is active, and the 48 KB is safe. No re-render needed.

### Memory Impact

−48 KB peak heap during page turns. The re-render uses only the already-allocated framebuffer.

### Trade-off

Re-rendering a cached EPUB page costs ~50–150 ms CPU. This happens *after* the grayscale is already on-screen, so the user sees no visible delay.

---

## Section 2: Device-Adaptive Image Dithering

### Problem

Both `AtkinsonDitherer` and `FloydSteinbergDitherer` in [BitmapHelpers.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/BitmapHelpers.h#L147-L161) use hardcoded "X4-tuned" quantization thresholds:

```cpp
// fine-tuned to X4 eink display
if (adjusted < 30)       → quantized=0, value=15
else if (adjusted < 50)  → quantized=1, value=30
else if (adjusted < 140) → quantized=2, value=80
else                     → quantized=3, value=210
```

The X3 panel has different electro-optical response (different gray LUT drive strengths: [EInkDisplay.cpp:132–163](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/open-x4-sdk/libs/display/EInkDisplay/src/EInkDisplay.cpp#L132-L163)). These thresholds produce sub-optimal gray separation on X3.

### Design

Extract quantization parameters into a flash-resident `DitherProfile`:

```cpp
struct DitherProfile {
  int thresholds[3];       // boundaries between the 4 gray levels
  int quantizedValues[4];  // target values per level (for error calculation)
};

static const DitherProfile kDitherProfileX4 = {
  {30, 50, 140},
  {15, 30, 80, 210}
};

// X3 tuning: wider separation between dark/light gray to match
// the X3 gray LUT's stronger WW/BW drive differential
static const DitherProfile kDitherProfileX3 = {
  {40, 80, 160},
  {20, 55, 120, 220}
};
```

Both `AtkinsonDitherer` and `FloydSteinbergDitherer` accept a `const DitherProfile&` at construction. The profile is selected once at init via `gpio.deviceIsX3()`.

### Files Changed

#### [MODIFY] [BitmapHelpers.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/BitmapHelpers.h)

- Add `DitherProfile` struct
- Parameterize `AtkinsonDitherer` and `FloydSteinbergDitherer` constructors to accept `const DitherProfile&`
- Replace hardcoded if/else chains with profile lookups

#### [MODIFY] [BitmapHelpers.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/BitmapHelpers.cpp)

- Define `kDitherProfileX4` and `kDitherProfileX3` as `static const`
- Add `getDitherProfile()` convenience function that checks `gpio.deviceIsX3()`

#### [MODIFY] [Bitmap.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/Bitmap.cpp)

- Pass device profile to ditherer construction

#### [MODIFY] Callers in [JpegToBmpConverter.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/JpegToBmpConverter/JpegToBmpConverter.cpp) and [PngToBmpConverter](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/PngToBmpConverter)

- Pass device profile when constructing ditherers

### Memory Impact

Two `DitherProfile` structs in flash (~28 bytes each). Zero extra RAM.

### X3 Threshold Tuning

The initial X3 values `{40, 80, 160}` / `{20, 55, 120, 220}` are starting points based on the X3 gray LUT drive ratios (WW=0x20 brief VDL pulse vs BW=0x80 moderate VDL pulse). Final tuning requires on-device testing with photographic images and gradient test patterns.

---

## Section 3: Image Sharpening Pre-Pass During Decode

### Problem

EPUB images lose perceived detail during the dithering step: error-diffusion dithering averages adjacent pixels, softening edges. By the time the 4-level grayscale result reaches the e-ink panel, fine details (text in images, line art, photo edges) look blurred.

### Design

A lightweight 1D unsharp-mask convolution applied row-by-row during JPEG/PNG → BMP decode, *before* dithering:

```cpp
// Applied to each pixel in the row buffer before dithering
// amount: 0.0 (off), 0.3 (subtle), 0.6 (strong)
inline uint8_t sharpenPixel(const uint8_t* row, int x, int width, float amount) {
  if (x == 0 || x >= width - 1) return row[x];  // skip edges
  int center = row[x];
  int neighbors = (row[x - 1] + row[x + 1]) / 2;
  int sharpened = center + static_cast<int>(amount * (center - neighbors));
  return static_cast<uint8_t>(std::clamp(sharpened, 0, 255));
}
```

- Operates on the existing decode row buffer — **zero additional buffer allocation**
- Applied after luminance conversion, before dithering `processPixel()`
- Controlled by a new setting `imageSharpening` (0=off, 1=subtle, 2=strong)

### Files Changed

#### [MODIFY] [BitmapHelpers.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/BitmapHelpers.h)

- Add `sharpenRow()` inline function

#### [MODIFY] [JpegToBmpConverter.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/JpegToBmpConverter/JpegToBmpConverter.cpp)

- Call `sharpenRow()` on the luminance row buffer before passing pixels to the ditherer

#### [MODIFY] [PngToBmpConverter](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/PngToBmpConverter) (row callback)

- Same: sharpen before dither

#### [MODIFY] [CrossPointSettings.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/CrossPointSettings.h)

- Add `uint8_t imageSharpening = 1;` (default: subtle)

#### [MODIFY] [SettingsList.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/SettingsList.h)

- Add image sharpening picker to Reader settings tab

### Memory Impact

Zero extra RAM. The sharpening operates in-place on the existing row buffer.

### Limitation

1D horizontal-only sharpening. A 2D kernel would be higher quality but requires buffering at least 3 rows (~2.4 KB for 800px width), which is possible but adds complexity. Start with 1D; upgrade to 2D later if needed.

---

## Section 4: UI Anti-Aliasing

### Problem

All UI screens (Home, Settings, File Browser, etc.) render in BW mode with `displayBuffer(FAST_REFRESH)`. The built-in fonts *already contain* 2-bit (4-level gray) glyph data — every font is built with `--2bit --darken-aa` — but the gray information is completely ignored outside the reader.

### Design

Add an opt-in post-display AA pass to priority UI screens. Uses the same re-render approach from Section 1, so **no `storeBwBuffer()` and no extra memory**.

#### New Helper

```cpp
// In a new file: src/activities/util/UIRenderUtils.h
namespace UIRenderUtils {

// Lightweight UI AA: upgrades BW text/icons to 4-level grayscale
// after the BW frame is already on-screen. Uses re-render (no storeBwBuffer).
// renderFn: callback that re-draws the screen content (same as the main render).
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

  // Re-render BW to restore framebuffer for next differential refresh
  renderer.clearScreen();
  renderFn();
  renderer.cleanupGrayscaleWithFrameBuffer();
}

}  // namespace UIRenderUtils
```

#### Adoption in Activities

Priority targets (most user-facing):

1. **[HomeActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/home/HomeActivity.cpp)** — after `renderer.displayBuffer()` at lines 1234 and 1286:
   ```cpp
   renderer.displayBuffer();
   if (SETTINGS.textAntiAliasing) {
     UIRenderUtils::renderUIAntiAliased(renderer, [&] {
       // Re-draw content (same calls as above minus displayBuffer)
     });
   }
   ```

2. **[SettingsActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/settings/SettingsActivity.cpp)** — after `renderer.displayBuffer()` at line 454

3. **[FileBrowserActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/home/FileBrowserActivity.cpp)** — after `renderer.displayBuffer()` at line 582

#### Setting Gate

Reuse the existing `SETTINGS.textAntiAliasing` toggle. When off, UI screens render BW-only as today — zero overhead.

### Memory Impact

Zero — uses re-render approach from Section 1.

### Trade-off

Each UI AA pass adds ~200–400 ms *after* the BW content is already visible. The user sees crisp BW immediately, then the gray "upgrade" smooths text edges. This is identical to the UX the reader already provides — users won't be surprised by it.

### Carousel Fast-Path

The HomeActivity carousel fast path (line 1180–1247) uses `memcpy` from pre-rendered frame buffers. AA for the carousel path would require storing separate grayscale-plane cached frames (2× the cache memory), which is too expensive. The carousel path stays BW-only; AA only applies to the slow-path full render.

---

## Section 5: Windowed Partial Updates for UI (Experimental, X4 Only)

### Problem

When a user scrolls through a settings list or file browser, the entire 800×480 screen is refreshed via `displayBuffer(FAST_REFRESH)` even though only two 30px-tall row stripes changed (the deselected row and newly selected row). Refreshing unchanged regions adds ghosting and reduces perceived crispness.

### Design

Use the existing `displayWindow()` implementation in [EInkDisplay.cpp:931–1005](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/open-x4-sdk/libs/display/EInkDisplay/src/EInkDisplay.cpp#L931-L1005) for list selection changes:

1. **Expose `displayWindow()` through the HAL stack:**
   - Uncomment the wrapper in [GfxRenderer.h:156](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/GfxRenderer.h#L156)
   - Add `displayWindow()` to [HalDisplay](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.h)

2. **List navigation windowed update:**
   - When the selected index changes by ±1 in a list, calculate the union bounding rect of the old and new selection
   - Call `displayWindow(x, y, w, h)` instead of `displayBuffer()`
   - The rows span full width (0 to `panelWidth`), satisfying the byte-alignment requirement

3. **Fallback:** For page scrolls, orientation changes, or any non-incremental navigation, fall back to full `displayBuffer()`.

### Scope Constraints

- **X4 only.** `displayWindow()` is not implemented in X3 mode. On X3, always use full `displayBuffer()`.
- **Start with SettingsActivity** as the test case. Expand to FileBrowserActivity and RecentBooksActivity after validation.
- **Opt-out via setting** if artifacts are reported: add a `windowedUpdates` toggle (default on for X4).

### Files Changed

#### [MODIFY] [GfxRenderer.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/GfxRenderer.h)

- Uncomment `displayWindow()` declaration (line 156)

#### [NEW] [GfxRenderer.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/GfxRenderer/GfxRenderer.cpp) addition

- Implement `displayWindow()` wrapper with orientation-aware coordinate mapping

#### [MODIFY] [HalDisplay.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.h) / [HalDisplay.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/lib/hal/HalDisplay.cpp)

- Add `displayWindow()` passthrough to `EInkDisplay::displayWindow()`

#### [MODIFY] [SettingsActivity.cpp](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/activities/settings/SettingsActivity.cpp)

- Track previous selection index
- On ±1 selection change, use windowed update for the changed rows

#### [MODIFY] [CrossPointSettings.h](file:///Users/hieuvh/Developer/projects/ereader/CrossInk/src/CrossPointSettings.h)

- Add `uint8_t windowedUpdates = 1;` (default on)

### Memory Impact

Zero extra RAM. `displayWindow()` reuses the existing framebuffer.

### Risk

This is the highest-risk section. `displayWindow()` is marked EXPERIMENTAL. Potential issues:
- RED RAM synchronization for the windowed region may not work correctly on all panel revisions
- Partial updates may accumulate ghosting faster than full refreshes
- Mitigation: opt-out toggle, X4-only, single activity rollout

---

## Verification Plan

### Automated Tests

```bash
pio run -e simulator    # Build compiles cleanly with all changes
```

### Manual Verification — Section 1 (BW Buffer Elimination)

1. Open an EPUB with text AA enabled
2. Turn pages rapidly — verify no visual corruption after grayscale upgrade
3. Monitor serial log for `bw_store`/`bw_restore` messages (should be absent from EPUB/TXT readers)
4. Open a TXT file — verify same behavior
5. Check sleep screen still uses `storeBwBuffer()` (serial log confirms)
6. Verify heap usage via serial log: `esp_get_free_heap_size()` should show ~48 KB more free during page turns

### Manual Verification — Section 2 (Device-Adaptive Dithering)

1. On X4: load an image-heavy EPUB — verify images look identical to current behavior
2. On X3: same book — verify gray levels show better separation (less muddy midtones)
3. Test with a gradient test image to confirm 4 distinct gray bands on each device

### Manual Verification — Section 3 (Image Sharpening)

1. Toggle `imageSharpening` between off/subtle/strong in Settings
2. Compare the same image at each setting — edges should be progressively crisper
3. Verify no ringing artifacts (white halos around dark edges) at "strong" level
4. Verify text-in-image remains legible

### Manual Verification — Section 4 (UI Anti-Aliasing)

1. Enable `textAntiAliasing` in Settings
2. Navigate Home screen — verify AA upgrade renders ~200-400ms after BW
3. Open Settings — verify list text gets AA upgrade
4. Open File Browser — same
5. Disable `textAntiAliasing` — verify UI screens render BW-only with no delay
6. Carousel fast-path: verify no regression (stays BW-only)

### Manual Verification — Section 5 (Windowed Updates)

1. On X4: open Settings, scroll up/down — verify only the selection rows refresh
2. Verify no ghosting on unchanged regions
3. Scroll past page boundary — verify full refresh fallback
4. On X3: verify windowed updates are disabled (full refresh always used)
5. Toggle `windowedUpdates` off — verify full refresh behavior returns

---

## Implementation Order

1. **Section 1** first — this is the enabler that frees 48 KB headroom
2. **Section 2** — device-adaptive dithering (independent, small change)
3. **Section 3** — image sharpening (independent, small change)
4. **Section 4** — UI AA (depends on Section 1 for the re-render pattern)
5. **Section 5** — windowed updates (independent, experimental, can be deferred)
