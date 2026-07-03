# Changelog

## [Unreleased]

### Added
- Added device-adaptive dithering profiles for X3 and X4 displays to optimize contrast and gray level separation.
- Added an in-place 1D horizontal unsharp-mask image sharpening pre-pass (Off, Subtle, Strong) for cover images.
- Added post-refresh grayscale anti-aliasing to Home and Settings UI to smooth text and borders.
- Added windowed partial updates for settings screen selection changes on X4 to make row highlights feel instant and fluid.
- Added a header clock on the Home screen, with a new **Time** section in Settings → System (24-hour or 12-hour format, UTC offset from −12 to +14).
- Added cold-boot NTP time sync on X4 (uses the last-connected Wi-Fi, runs once, then disconnects).
- Added DS3231 hardware RTC support on X3 (read and write).
- Added manual time entry in Settings → System (on-device editor and via the web Settings page's "Set to my browser's time" button).
- Added a **Button Hints** toggle in Settings → Display to show or hide the button label bar at the bottom of the screen.
- Added **Quicksand** as a third built-in reading font (sizes 8/10/12/14/16). Regular uses the Medium weight for stronger strokes on e-ink; italic body text falls back to the upright weight since Quicksand ships no italic master.
- Switched the UI font from Inter to **Quicksand SemiBold** (Regular slot) + **Quicksand Bold** (Bold slot) so labels and headers carry more weight on the e-ink panel.

### Changed
- Refactored EPUB/TXT Reader anti-aliasing to use a post-AA re-render pass, eliminating the 48 KB static backup buffer to save precious heap space and improve stability.
- Reader page turns now skip the grayscale anti-aliasing "upgrade" pass when another page turn is already queued — rapid skimming stays snappy, and the AA refresh only runs once you stop on a page.
- Tightened menu navigation timings: button polling cadence is now 5 ms (was 10 ms), hold-to-scroll engages at 300 ms (was 500 ms), and continuous nav advances every 250 ms (was 500 ms) — long settings/file-browser lists feel noticeably more responsive.
- Restored bold weight on Lyra section headers in Settings now that the Vietnamese first-character glyph bug is no longer relevant.

### Fixed
- Fixed the old settings tab remaining visible under the new one when switching tabs (or flipping list pages) with anti-aliasing enabled: a fast differential refresh cannot erase the previous text's gray anti-aliased pixels, so full settings re-renders now use a cleaning half refresh before the anti-aliasing pass. Selection moves within a page keep the instant windowed update; moves that flip the page now take the clean full-refresh path.
- Fixed book covers overlapping each other after a few carousel swaps: when the centered book changes, the carousel band is blanked and displayed before the new cover is drawn (the same double-refresh technique the reader uses for image pages), so each cover starts from a clean white band.
- Fixed gray smearing ("burn-in") on Home and Settings with anti-aliasing enabled: plain fills, lines, and icon blits drawn during the grayscale passes were flagging whole regions — including the entire restored cover frame on Home — as "drive to dark gray". Flat primitives now render only in the black-and-white pass; the grayscale passes carry nothing but anti-aliased text and properly classified images.
- Fixed image overlapping when switching between the Home screen (book cover carousel) and Settings: screen transitions used fast differential refreshes that cannot clean the previous screen's residue. The first render after entering Home or Settings now uses a full-drive half refresh.
- Fixed the automatic anti-ghosting refresh never triggering on anti-aliased screens: the grayscale pass was resetting the consecutive-fast-refresh counter even though it cleans nothing. Windowed settings updates now promote to a cleaning half refresh after 8 updates (per the ghosting design spec); full-buffer refreshes keep the 32 threshold so the reader's own refresh-frequency setting stays in charge.
- Fixed windowed settings updates never appearing on real hardware: the window's panel coordinates were not byte-aligned, so the display controller silently rejected every windowed refresh (selection appeared frozen until the periodic full refresh).
- Fixed the windowed settings update refreshing the wrong rows on the Lyra themes: the region math assumed the base theme's 30 px rows and fixed section-header padding, while Lyra draws 40 px rows with variable-height headers. The update now refreshes the whole list strip, which is correct for every theme.
- Fixed toggling a setting that changes global layout (e.g. UI Theme, Button Hints) leaving stale chrome on screen — value changes now always take the full-refresh path; windowed updates apply only to selection movement.
- Fixed the first windowed update after an anti-aliased screen corrupting anti-aliased text across the whole display (the controller's RAM still held the grayscale plane; HalDisplay now rebases it with one full differential refresh first).
- Fixed status bar and bookmark/finished toasts ghosting on the next page turn after an anti-aliased page: the post-AA baseline re-render only redrew the page content, so the differential refresh treated the status bar area as blank.
- Fixed Home and Settings screens showing the previous frame with anti-aliasing enabled: the grayscale pass only drives the gray edge pixels, so the black-and-white frame must be displayed first (restores the brief two-phase update, which is inherent to the AA pipeline).
- Fixed display burn-in (ghosting) and Settings menu selection navigation lag (implemented automatic E-ink refresh promotion in HalDisplay and limited anti-aliasing to page transitions).
- Fixed home screen menu item label text jumping and smearing (vertical misalignment of clearing and text y-coordinates in Lyra Carousel overlay rendering).
- Fixed intermittent crash when opening a book from the Lyra Carousel home screen (race condition between the main task freeing carousel frame buffers and the render task reading them).
- Fixed Lyra Carousel theme showing a solid black square instead of an icon for selected items in lists that use subtitle rows (e.g. Recent Books), for icons that only have 24px variants.
- Fixed selected button icons appearing as a solid black square instead of a white icon on black during font cache scan passes.

### Removed
- Removed Bionic Reading and Guide Dots features from the EPUB reader.
- Removed all UI languages except English and Vietnamese.
- Removed the **XTC Status Bar** setting (Settings → Display → Customize Status Bar) and the matching top/bottom overlay on XTC pages. The XTC reader was the only consumer, the overlay was off by default, and the rendering code had no other use — XTC pages now always render full-bleed.
- Removed **Vietnamese** as a UI language. The Settings → System → Language entry and the `LanguageSelectActivity` screen are also gone now that English is the only built-in option; existing settings files with `"language": "VI"` load cleanly and fall back to English.

## [v1.2.10]

### Added
- Added a `Recent Books View` setting so the dedicated Recent Books screen can switch between the classic list and a 3x3 cover grid.
- Added separate orientation-aware controls for front and side reader buttons, with front-button modes for nav-only or all-button inversion.
- Added `Tilt Page Turn` as a selectable reader shortcut for power-button short/long press and the front menu long-press action on devices with a tilt sensor.
- Added orientation changes to the side-button long-press action, with Up rotating counterclockwise and Down rotating clockwise.
- Added EPUB `<hr>` rendering so horizontal rules display as visible separators instead of being ignored.
- Added EPUB heap diagnostics around section rebuilds, image extraction, page serialization, and sleep-cache rebuilds to make low-memory crashes easier to trace.
- Added a per-session auto page turn interval picker with values from 5 to 120 seconds.
- Added reader font coverage for block redactions, black-square ornaments, Greek category letters, and turned-comma punctuation (PR #104).
- Added a file-browser Home/Back long-press action for toggling hidden files and folders.
- Added simulator tools for testing sleep/wake behavior and smoke-testing common screens and EPUB reader menus.

### Changed
- Reduced Controls settings section spacing so the grouped controls fit better on X3 screens.
- Temporarily hid the Lyra Carousel theme option unless `CROSSINK_ENABLE_LYRA_CAROUSEL=1` is set at build time, and migrated existing disabled selections back to Lyra while the carousel remains experimental.
- Made front reader long-press actions trigger when the hold delay is reached while normal page turns still trigger on release.
- Use the fast EPUB spine/TOC indexing path for books with 300+ spine entries so heavily split books build `book.bin` faster on first open.
- Allow the web file manager and WebDAV to browse dot-prefixed hidden files when hidden files are enabled, matching the device file browser.

### Fixed
- Fixed X3 power-button wake filtering so a short tap does not wake the device when the configured wake action requires a long press.
- Fixed RoundedRaff home-menu navigation so Settings remains reachable when the inline Continue Reading row is visible.
- Reduced persistent SD-card font advance-cache memory so custom fonts leave more heap available for EPUB rendering.
- Release optional SD-card font caches before EPUB image extraction only when heap is tight so custom fonts and image-heavy chapters can coexist more reliably without unnecessary cache rebuilds.
- Fixed RoundedRaff keyboard and button-hint rendering so number-row symbols and UTF-8 labels no longer overlap or disappear.
- Fixed WiFi scan/connect screens so users can back out while a scan or connection attempt is in progress.
- Fixed folder delete long-press timing so deletion triggers after the hold delay instead of on release.
- Fixed missing-glyph rendering so compact UI fonts show a visible replacement symbol even when they do not include `U+FFFD`.
- Fixed KOReader Sync authentication handling with better validation and clearer diagnostics when a server or proxy returns non-JSON content.
- Fixed EPUB redaction and whitespace rendering by preserving whitespace-only XHTML text nodes and rendering simple black CSS backgrounds for inline spans.
- Fixed EPUB list bullets so they stay attached to the first paragraph in `<li><p>...</p></li>` list items.
- Fixed EPUB image scaling, low-memory image fallback, and thumbnail generation so image-heavy books are less likely to crash or reuse stale dimensions.
- Fixed EPUB section rebuilds so image-heavy chapters use less temporary memory while laying out text after inline images.
- Fixed EPUB low-memory stability by skipping optional silent next-chapter indexing and sleep-page cache rebuilds when heap is already tight.
- Fixed EPUB image handling so shared memory budgets suppress inline images and decoder work earlier under heap pressure.
- Fixed EPUB layout stability for books with very long base64-like text runs by avoiding expensive hyphenation fallback work.
- Fixed EPUB section indexing so low-memory text layout fails safely with a malformed-book warning and Home exit path instead of aborting when custom font preflight or CSS-heavy chapters exhaust heap.
- Fixed EPUB cache validation so Crossink rebuilds `book.bin`, `sections/*.bin`, and CSS rule caches written by other CrossPoint forks instead of treating matching version numbers as compatible.
- Fixed EPUB CSS loading and page-cache handling so low-memory CSS parsing, truncated SD writes, invalid serialized strings, and bad temp-cache promotion fail safely.
- Fixed a Home crash after clearing reading cache by skipping optional EPUB thumbnail rebuilds when the source EPUB cache is missing.
- Fixed reader prewarm behavior by skipping image decoding, keeping mixed-style font glyphs cached together, and avoiding section rebuilds for render-quality-only option changes.
- Fixed a KOReader Sync crash when starting sync from inside an EPUB reader session.
- Fixed concurrent render/storage crashes by serializing `GfxRenderer` scratch-buffer access, shared SPI bus access, and failed SPI lock cleanup.
- Fixed Recent Books, EPUB/XTC thumbnail caches, Lyra Carousel snapshots, and deleted-folder metadata so they stay in sync when cache files change or are removed.
- Fixed XTC covers in the Recent Books grid so they fill cover slots instead of appearing letterboxed when the first page has a different aspect ratio.
- Fixed Lyra Carousel cache handling so grid-sized thumbnails are not reused as carousel covers and SD snapshots are skipped after low-RAM frame-cache fallback.
- Fixed simulator build configuration so SDL2 and simulator-provided network/OTA shims compile cleanly.

## [v1.2.9.1] - 2026-05-03

### Changed
- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Fixed
- Fix power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions. Those that had short-press power button to act as sleep saw unstable behavior previously. This should be fixed now
- Fix a potential crash when using `Go to %` in EPUBs
- Fix a potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid
