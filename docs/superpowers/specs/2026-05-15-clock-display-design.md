# Clock Display — Design Spec

- **Date:** 2026-05-15
- **Branch:** `new_theme`
- **Status:** Draft, pending implementation
- **Devices:** X3 (DS3231 hardware RTC) and X4 (ESP32-C3 internal RTC)

## 1. Goal

Show the current time on the left side of the Home screen header, and add a Settings category that lets the user pick the time format, timezone, sync via NTP, or set the time manually. SCOPE.md already lists "Clock Display (device dependent)" as in-scope; this spec fills in the user-facing UX and the cross-device implementation.

## 2. Constraints carried in from the project

- ESP32-C3, ~380 KB usable RAM, single core. Stability over features.
- E-ink display: full refresh flashes, partial refresh ghosts. Refreshes are a budget.
- X4 has no battery-backed RTC: time is lost on cold boot. Per `SCOPE.md` the X4 internal RTC drifts noticeably across deep sleep.
- X3 has a DS3231 (probed at boot in `lib/hal/HalGPIO.cpp:78`) but no driver code yet.
- "Active connectivity" is out-of-scope per `SCOPE.md`. Background Wi-Fi tasks are not allowed; on-demand sync is.
- HAL discipline (`CLAUDE.md`): app code uses HAL classes, never SDK classes directly.
- `LyraTheme::drawHeader` does not call `BaseTheme::drawHeader` (CONTEXT.md): both must be edited.
- POSIX TZ inversion gotcha (CONTEXT.md): we sidestep it by storing UTC and applying an integer-hour offset ourselves; we never feed strings to `setenv("TZ", …)`.

## 3. Decisions (resolved during brainstorming)

| Question | Decision |
|---|---|
| Time source on X4 | NTP on cold boot only (one async attempt). Manual "Sync now" button after that. No per-wake or scheduled re-sync. |
| Time source on X3 | DS3231 hardware RTC. Authoritative across sleep and power loss. NTP only on user request, only if DS3231 has invalid time on first run. |
| Fallback when NTP fails | Show last-known time persisted to `/.crosspoint/time.bin`. Manual override available via on-device Settings. |
| Display format | 24h `HH:MM` default. Settings toggle for 12h `H:MM AM/PM`. |
| Timezone | Integer-hour UTC offset, range −12…+14 (27 entries). No DST handling, no named zones. |
| Refresh cadence | Render only on Home redraw. No background timer, no per-minute partial refresh. |
| Manual entry | On-device dedicated `SetTimeActivity` (HH:MM editor) AND web UI ("Set to my browser's time" button). |
| Where shown | Home screen header only. Other screens keep their existing headers. |
| v1 device support | Both X3 and X4. Adds a DS3231 read/write driver in HAL. |

## 4. Architecture

```
                     ┌─────────────────────────┐
                     │   HomeActivity::render  │  reads time on every redraw
                     └────────────┬────────────┘
                                  │ formatLocal(buf, cap) → bool
                                  ▼
                     ┌─────────────────────────┐
                     │      TimeService        │  app-layer singleton
                     │  - formatLocal(buf,cap) │
                     │  - onNtpSynced(epoch)   │
                     │  - onManualSet(epoch)   │
                     │  - hasValidTime()       │
                     └────────────┬────────────┘
                                  │ uses
                                  ▼
                     ┌─────────────────────────┐
                     │    RtcBackend (HAL)     │  abstract interface
                     └──────┬──────────────┬───┘
                            │              │
                ┌───────────▼──┐   ┌───────▼───────────┐
                │ HalRtcDS3231 │   │ HalRtcInternal    │
                │   (X3 only)  │   │   (X4 default)    │
                └──────────────┘   └───────────────────┘

   ┌───────────────────────┐         ┌──────────────────────────┐
   │ NtpSyncService        │  on    │ TimePersistenceStore     │
   │ - syncOnce(timeoutMs) │ success │ - lastSyncedUtc (SD)     │
   │ - reuses lastConn SSID│ ───────▶│ - debounced write        │
   └───────────────────────┘         └──────────────────────────┘
        ▲                                          ▲
        │ called by                                │ read by TimeService.boot()
        │                                          │ if RTC has no valid time
        │
   - main.cpp setup() (cold boot, async via FreeRTOS task)
   - SettingsActivity "Sync now" action (foreground, modal popup)
```

### 4.1 New components

- `lib/hal/HalRtc.h` — `RtcBackend` interface with `bool readUtcEpoch(int64_t* out)` and `bool writeUtcEpoch(int64_t epoch)`.
- `lib/hal/HalRtcDS3231.{h,cpp}` — I2C driver for the DS3231 at `I2C_ADDR_DS3231` (0x68). BCD encoding for the time registers. Constructor probes; on probe failure, owner falls back to internal.
- `lib/hal/HalRtcInternal.{h,cpp}` — wraps `gettimeofday`/`settimeofday` from the ESP-IDF newlib.
- `src/services/TimeService.{h,cpp}` — singleton, owns the `RtcBackend`, the `TimeSource` flag, and a FreeRTOS mutex. The only API the app sees.
- `src/services/NtpSyncService.{h,cpp}` — extracted from `KOReaderSyncActivity.cpp:25-53`. After this change `KOReaderSyncActivity` calls into the service rather than duplicating SNTP setup. Returns `SyncResult { ok, epoch, error }`.
- `src/TimePersistenceStore.{h,cpp}` — parallels `BookmarkStore`/`WifiCredentialStore`. SD-backed file `/.crosspoint/time.bin`. Atomic write via tmp + rename. CRC-validated.
- `src/activities/settings/SetTimeActivity.{h,cpp}` — full-screen activity for HH:MM entry.
- Test files under `test/time_format/` and `test/time_persistence/` (host-compiled, see §8).

### 4.2 Modified files

- `src/CrossPointSettings.h` — add `bool showHeaderClock`, `uint8_t timeFormat`, `int8_t utcOffsetHours`.
- `src/SettingsList.h` — add `STR_CAT_TIME` category and 5 entries (see §5).
- `src/activities/settings/SettingsActivity.cpp` — wire `SettingAction::SyncTimeNow` and `SettingAction::SetTimeManual`.
- `src/components/themes/lyra/LyraTheme.cpp::drawHeader` — draw time on the left when `title == nullptr` and `SETTINGS.showHeaderClock` is on.
- `src/components/themes/BaseTheme.cpp::drawHeader` — same change for the classic theme (`LyraTheme::drawHeader` does not chain into `BaseTheme::drawHeader`, per CONTEXT.md).
- `src/main.cpp` — call `TimeService::instance().boot(deviceType)` once during setup.
- `lib/I18n/translations/english.yaml` — add new string IDs (see §5). `english.yaml` is the only remaining translation file in this branch, so the addition is one-file.
- `src/network/html/SettingsPage.html` — web UI fields and button (see §5.6). `scripts/build_html.py` regenerates the corresponding `SettingsPageHtml.generated.h`.
- `src/network/CrossPointWebServer.cpp` — register `GET /api/time`, `POST /api/time`, `POST /api/time/sync`.
- `CHANGELOG.md` — `Added` entry per CLAUDE.md.

### 4.3 Header rendering rule

In both `LyraTheme::drawHeader` and `BaseTheme::drawHeader`:

```cpp
if (title == nullptr && SETTINGS.showHeaderClock) {
    char buf[16];
    if (TimeService::instance().formatLocal(buf, sizeof(buf))) {
        renderer.drawText(UI_12_FONT_ID,
                          rect.x + LyraMetrics::values.contentSidePadding,
                          rect.y + LyraMetrics::values.batteryBarHeight + 3,
                          buf, true, EpdFontFamily::BOLD);
    }
}
```

Rationale: title is suppressed on Home only, so the clock automatically appears only on Home. Other screens that pass a title keep their existing layout — no visual collisions, no per-screen redesign.

### 4.4 Stack discipline

- `TimeService::boot()` spawns a 4 KB FreeRTOS task ("ntp_boot") for the cold-boot NTP attempt on X4. The task self-deletes after success/failure/timeout (CLAUDE.md: "Delete FreeRTOS tasks before … destroyed").
- All header reads use a 16-byte `char[]` on caller stack — no heap, no `std::string` in the hot path (CLAUDE.md rule).
- `TimeService` allocates one `std::unique_ptr<RtcBackend>` once at boot, no churn.

## 5. Settings UI

### 5.1 New settings category

`STR_CAT_TIME` ("Time"), placed between `STR_CAT_DISPLAY` and `STR_CAT_READER`.

### 5.2 Settings entries (in display order)

| Label | Type | Storage | Notes |
|---|---|---|---|
| Show clock in header | Toggle | `CrossPointSettings::showHeaderClock` (bool, default `true`) | Read in `drawHeader` |
| Time format | Enum | `CrossPointSettings::timeFormat` (uint8, 0=24h default, 1=12h) | Read in `formatLocal` |
| Time zone | Enum | `CrossPointSettings::utcOffsetHours` (int8, range −12…+14, default 0) | 27 enum values `STR_TZ_UTC_M12`…`STR_TZ_UTC_P14` |
| Sync time now | Action | `SettingAction::SyncTimeNow` | Opens a modal popup that runs `NtpSyncService::syncOnce` |
| Set time manually | Action | `SettingAction::SetTimeManual` | Launches `SetTimeActivity` |

### 5.3 New i18n string IDs (english.yaml)

- `STR_CAT_TIME` — "Time"
- `STR_SHOW_HEADER_CLOCK` — "Show clock in header"
- `STR_TIME_FORMAT` — "Time format"
- `STR_TIME_FORMAT_24H` — "24-hour"
- `STR_TIME_FORMAT_12H` — "12-hour"
- `STR_TIMEZONE` — "Time zone"
- `STR_SYNC_TIME_NOW` — "Sync time now"
- `STR_SET_TIME_MANUAL` — "Set time manually"
- `STR_SYNCING` — "Syncing time…"
- `STR_CONNECTING_WIFI` — "Connecting to Wi-Fi…"
- `STR_SYNC_FAILED` — "Sync failed"
- `STR_SYNC_OK` — "Time synced"
- `STR_NO_SAVED_WIFI` — "No saved Wi-Fi network"
- `STR_TZ_UTC_M12` … `STR_TZ_UTC_P14` — 27 literal labels `"UTC-12"` … `"UTC+14"` (no translation needed; literal in every locale).

### 5.4 SetTimeActivity

Pattern: heap-allocated activity following `ButtonRemapActivity`. Owns a `ButtonNavigator`. Two integer fields, current selection cursor, button hints at the bottom.

```
┌──────────────────────────────────────────────┐
│ Set time                                     │  ← drawHeader, title="Set time"
│──────────────────────────────────────────────│
│                                              │
│                  ┌──┐ ┌──┐                  │
│                  │14│:│32│                  │   large numeric digits, two fields
│                  └──┘ └──┘                  │   selected field is highlighted
│                                              │
│──────────────────────────────────────────────│
│  ◀ ▶ Field   ▲ ▼ Adjust   OK Save   ← Back  │
└──────────────────────────────────────────────┘
```

- Always 24h editor, regardless of the user's chosen `timeFormat`. Avoids an AM/PM third field for v1.
- Initial values: current local time if `TimeService::hasValidTime()`, else `00:00`.
- Up/Down: ±1 with wrap. Long-press acceleration only if `ButtonNavigator` already supports it (do not invent a new mechanism for this).
- OK: convert `(hours, minutes)` plus `dateGuess` plus `utcOffsetHours` into a UTC epoch, call `TimeService::onManualSet(epoch)`, return.
- Back: discard, return.
- `dateGuess`: current date the RTC believes; or `lastSyncedUtc + (millis() - lastSyncedAtMs)` if RTC is empty; or `2026-01-01` ultimate fallback. Date editing on-device is out of scope for v1.

### 5.5 "Sync time now" popup flow

Foreground modal popup activity that owns the sync attempt end-to-end:

```
   tap "Sync time now"
          │
          ▼
   ┌─────────────────────┐
   │ "Connecting to      │  Wi-Fi connect, 8 s timeout
   │  Wi-Fi…"            │  uses lastConnectedSsid from WifiCredentialStore
   │            [Cancel] │  Cancel = Back press
   └─────────┬───────────┘
             │
       ┌─────┴─────┐
   no creds    creds ok
       │           │
       ▼           ▼
  ┌────────┐  ┌──────────────┐
  │ "No    │  │ "Syncing      │  SNTP poll, 5 s timeout
  │ saved  │  │ time…"        │
  │ Wi-Fi" │  └──────┬────────┘
  │ [OK]   │         │
  └────────┘   ┌─────┴─────┐
              fail        ok
               │           │
               ▼           ▼
         ┌──────────┐  ┌────────────────────┐
         │ "Sync    │  │ "Time synced.       │  auto-dismiss after 1.5 s
         │  failed" │  │  Now 14:32"         │
         │ [OK]     │  └────────────────────┘
         └──────────┘
```

- Wi-Fi is always disconnected on popup exit (success, fail, or cancel). Matches SCOPE.md "no background Wi-Fi" rule.
- Cancel: sets a flag the SNTP poll loop checks each 100 ms tick (existing pattern in `KOReaderSyncActivity.cpp:39`).
- "Sync now" ignores the `TimeSource::ManuallySet` guard — explicit user request overrides.

### 5.6 Web UI

Added to the existing settings/files HTML page:

- Read-only field "Device time": polled every 5 s while page is open via `GET /api/time` returning `{ epoch, iso }`.
- Button "Set to my browser's time": JS sends `POST /api/time` with `{ epochMs: Date.now() }`. Server divides by 1000 and calls `TimeService::onManualSet`.
- Button "Sync via NTP": `POST /api/time/sync` returns `{ ok, epoch?, error? }`.

No freeform HH:MM input on the web — "Set to browser time" subsumes that need.

## 6. Data flow

### 6.1 Cold boot sequence

```
main.cpp setup():
  1. halGpio.detectDeviceType() → returns X3 or X4
  2. SETTINGS.load() → utcOffsetHours, timeFormat, showHeaderClock
  3. TimeService::instance().boot(deviceType):
       a. Construct RtcBackend:
            X3 → make_unique<HalRtcDS3231>() (probes I2C; falls back to internal on failure)
            X4 → make_unique<HalRtcInternal>()
       b. persistenceStore.load()
       c. if !rtc->hasValidTime() && persistenceStore.hasLastSynced():
            rtc->writeUtcEpoch(persistenceStore.lastSyncedUtc);
            currentSource = TimeSource::RestoredFromNvs;
          else if rtc->hasValidTime():
            currentSource = (deviceType == X3) ? TimeSource::NtpSynced /* trust DS3231 */
                                               : TimeSource::RestoredFromNvs;
       d. Decide whether to spawn the cold-boot NTP task:
            spawn IF deviceType == X4
                   OR (deviceType == X3 AND !rtc->hasValidTime())
            AND  WifiCredentialStore::instance().hasAny()
            Task: 4 KB stack, priority IDLE+1, "ntp_boot", self-deletes
  4. ... rest of setup, HomeActivity launches normally ...
```

The cold-boot NTP task is fire-and-forget. Home renders immediately with whatever time the RTC has (or nothing). When NTP completes, the next user-driven Home redraw picks up the new time.

### 6.2 Time-read path (per Home redraw)

```
HomeActivity::render → LyraTheme::drawHeader(title=nullptr)
  → if SETTINGS.showHeaderClock:
       char buf[16]; if (TimeService::formatLocal(buf, sizeof(buf))) drawText(...)
```

`formatLocal`:
```cpp
bool TimeService::formatLocal(char* out, size_t cap) {
    LockGuard g(mutex);
    int64_t utcEpoch;
    if (!rtc->readUtcEpoch(&utcEpoch)) return false;
    if (utcEpoch < kMinValidEpoch || utcEpoch > kMaxValidEpoch) return false;
    const int64_t local = utcEpoch + int64_t(SETTINGS.utcOffsetHours) * 3600;
    const int hour24 = (local / 3600) % 24;
    const int minute = (local / 60) % 60;
    if (SETTINGS.timeFormat == 0) {
        return snprintf(out, cap, "%02d:%02d", hour24, minute) > 0;
    }
    const int hour12 = ((hour24 + 11) % 12) + 1;
    return snprintf(out, cap, "%d:%02d %s", hour12, minute, hour24 < 12 ? "AM" : "PM") > 0;
}
```

### 6.3 NTP success write order

```
NtpSyncService::syncOnce → SyncResult { ok, epoch, error }
    ↓ (on ok)
TimeService::onNtpSynced(epoch):   // cold-boot task path
    LockGuard g(mutex);
    if (currentSource != TimeSource::ManuallySet) {
        rtc->writeUtcEpoch(epoch);          // hardware first
        persistenceStore.write(epoch);      // NVS second
        currentSource = TimeSource::NtpSynced;
    }
```

Manual-Sync-Now path: same write order, no `ManuallySet` guard (explicit user action).

### 6.4 Manual set write order

```
SetTimeActivity OK pressed (or POST /api/time):
    int64_t newUtc = computeUtcFromHHMM(hh, mm, dateGuess, SETTINGS.utcOffsetHours);
    TimeService::onManualSet(newUtc):
        LockGuard g(mutex);
        rtc->writeUtcEpoch(newUtc);
        persistenceStore.write(newUtc);
        currentSource = TimeSource::ManuallySet;
```

### 6.5 Persistence file format

Path: `/.crosspoint/time.bin` (SD-backed, follows `BookmarkStore`/`WifiCredentialStore` pattern).

```cpp
struct TimePersistencePayload {
    uint32_t magic;              // 0x54494D45 'TIME'
    uint16_t version;            // 1
    uint16_t reserved;
    int64_t  lastSyncedUtc;      // seconds since epoch
    uint32_t crc32;
};
```

Atomic write: write to `/.crosspoint/time.bin.tmp`, fsync, rename to `/.crosspoint/time.bin`. Mirrors existing stores.

Debounce: `write(epoch)` is a no-op if `|epoch − lastWritten| < 60 s`. Protects NVS endurance.

Written from exactly two places: `TimeService::onNtpSynced` and `TimeService::onManualSet`. Never on a tick, never on shutdown.

### 6.6 TimeSource flag

```cpp
enum class TimeSource : uint8_t { None, RestoredFromNvs, NtpSynced, ManuallySet };
```

Resolves the cold-boot-vs-manual race: user opens "Set time" within the 8 s NTP window, taps OK, then NTP completes 2 s later. Without the flag, NTP would silently overwrite the manual entry. With it, the cold-boot task bails when it sees `ManuallySet`.

## 7. Error handling

### 7.1 Failure matrix

| Failure | Detection | Behavior | User-visible |
|---|---|---|---|
| DS3231 unresponsive on X3 (I2C timeout, dead battery, missing) | Constructor probes `DS3231_SEC_REG`; read also checks I2C ack | Construction falls back to `HalRtcInternal` with `LOG_ERR("RTC", "DS3231 unavailable, falling back to internal")` | Header empty on first cold boot until NTP/manual |
| DS3231 returns sub-2025 epoch (battery loss, fresh module) | `kMinValidEpoch` floor in `readUtcEpoch` | Treated as "no valid time"; cold-boot NTP runs on X3 in this case | Header empty until NTP/manual |
| `HalRtcInternal` returns epoch 0 (fresh chip) | `kMinValidEpoch` floor | `formatLocal` returns false | Header empty |
| `/.crosspoint/time.bin` corrupt | Magic, version, and CRC check in `load` | Treat as "no last-known time"; delete the file so next write starts clean | None |
| Power loss during persistence write | Atomic tmp + rename | Old file intact or new file fully landed | None |
| NTP DNS / network failure | `SyncResult.ok == false`, error category populated | Cold boot: log only. "Sync now": popup "Sync failed — check Wi-Fi" with [OK] | Only when explicitly requested |
| Wi-Fi credentials missing | `WifiCredentialStore::credentials.empty()` checked before any Wi-Fi attempt | Cold boot: skip task entirely. "Sync now": popup "No saved Wi-Fi" with [OK] | Clear actionable message |
| Wi-Fi connect fails | `WiFi.status() != WL_CONNECTED` after 8 s | Same as DNS failure path | Same message |
| NTP returns absurd epoch | Bounds: reject `< kMinValidEpoch` or `> kMaxValidEpoch` | Treat as failure; do not write | Same "Sync failed" message |
| User cancels "Sync now" mid-flight | Back press while popup showing | `NtpSyncService::cancel()` flag checked each poll tick; `WiFi.disconnect()` always called in popup `onExit` | Popup closes silently |
| Two writers race (cold-boot task + Sync-now) | `xSemaphoreCreateMutex()` in `TimeService` | Last writer wins; reads are atomic | None |
| User opens "Set time" while cold-boot NTP still running | `TimeSource::ManuallySet` flag (§6.6) | Manual entry sticks; NTP task self-deletes without writing | None |
| `utcOffsetHours` out of range (corrupt settings) | `Settings::load` clamps to [-12, +14] | Clamp, log if fired | None |
| `timeFormat` enum > 1 | Same clamp, default to 0 | Default to 24h | None |
| Settings file missing entirely | Existing settings-load fallback gives defaults | Defaults: 24h, UTC, clock visible | Header may be hours off until user sets timezone |
| Time visibly jumps when boot NTP completes | Inherent | Next redraw shows new time, no animation | One-time visible jump on first navigation after boot |
| User sets manual time in the past | None — accepted as valid | Stored as-is | None — user is the authority |
| `SetTimeActivity` exited via Back | Activity lifecycle | Discard in-progress HH/MM | None |
| DS3231 write fails on X3 | `writeUtcEpoch` returns false | Log error; persistence still writes (next boot can restore) | Settings popup "Couldn't update RTC" with [OK] |
| Web POST /api/time arrives during cold-boot NTP | Same race as on-device manual flow | Web POST sets `ManuallySet`; cold-boot bails | None |
| Two browsers POST /api/time near-simultaneously | TimeService mutex serializes | Last writer wins | None |

### 7.2 Sanity-check constants

```cpp
// TimeService.h
constexpr int64_t kMinValidEpoch = 1735689600;   // 2025-01-01T00:00:00Z
constexpr int64_t kMaxValidEpoch = 4102444800;   // 2100-01-01T00:00:00Z
```

Used by `formatLocal`, `RtcBackend::readUtcEpoch`, `NtpSyncService` (reject absurd responses), `TimePersistenceStore::load`.

### 7.3 Watchdog / hang protection

- `NtpSyncService::syncOnce(timeoutMs)` is the only blocking thing: 8 s Wi-Fi connect + 5 s SNTP = 13 s worst case.
- Cold-boot task hard-deletes itself after the ceiling whether or not SNTP returned, so it cannot pin a stack slot.
- "Sync now" popup polls `WiFi.status()` and `sntp_get_sync_status()` with `vTaskDelay(100ms)` and processes button events on each tick (existing pattern in `KOReaderSyncActivity.cpp:39`).

### 7.4 Out of scope for v1 (explicit non-handling)

- Daylight saving time
- Date editing on-device
- Per-screen clock (only Home)
- Battery-aware suppression of cold-boot NTP
- Showing seconds (would force a per-minute partial-refresh cadence we explicitly rejected)

## 8. Testing strategy

### 8.1 Layer 1 — Host-compiled unit tests

Two self-contained programs under `test/time_format/` and `test/time_persistence/`, built with a shell script in the existing pattern (`test/run_release_json_parser_test.sh`). Bare `assert()`, single `main()` per file, returning non-zero on first failure. No new framework introduced — the project's existing test convention.

`test/time_format/test_time_format.cpp`:
- 24h render: `(epoch=0, offset=0)` → `"00:00"`; `(epoch=3600*14+60*32, offset=0)` → `"14:32"`; `(epoch=0, offset=-7)` → `"17:00"` (wrap into prior day; only HH:MM matters).
- 12h render: `0:00` → `"12:00 AM"`, `12:00` → `"12:00 PM"`, `13:05` → `"1:05 PM"`, `23:59` → `"11:59 PM"`.
- Sanity floor: `kMinValidEpoch - 1` → `formatLocal` returns false.
- Buffer-too-small: `cap=4` → returns false, doesn't write past buffer.
- Offset boundary: `+14`, `-12` both produce sensible HH:MM.

`test/time_persistence/test_time_persistence.cpp`:
- Round-trip: write `epoch=1747000000`, read back, equal.
- Magic mismatch → `load` returns false; file is then deleted.
- Version=999 → `load` returns false.
- CRC tampered → `load` returns false.
- Truncated file (8 bytes) → `load` returns false.
- Atomic write: simulate crash mid-write — original file intact.
- Debounce: two `write(epoch)` within 60 s → second is a no-op.

`TimeService::formatLocal` and `TimePersistenceStore` must be written as plain C++ over small abstractions (`RtcBackend` for one, `FILE*`-style I/O for the other) so they're host-buildable without `Arduino.h`. Anything needing `Arduino.h` lives in `HalRtcInternal` / `HalRtcDS3231`.

Run via a new `test/run_time_tests.sh` matching the existing script shape.

### 8.2 Layer 2 — Simulator (`pio run -e simulator`)

- `HalRtcDS3231` is added to `lib_ignore` for the simulator env (matches how `hal`, `PNGdec`, `JPEGDEC` are ignored). X3 codepath never reached in simulator.
- `HalRtcInternal` simulator implementation: read returns `time(nullptr) + override`; write stores `override = newEpoch - time(nullptr)`. Lets manual-set behavior be observable without requiring root for `settimeofday`.
- `NtpSyncService` simulator stub: returns `SyncResult{ ok=false, error=NotSupportedInSimulator }`. Documented as expected, like `JPEGDEC fallback: open failed (err=-1)`.
- `TimePersistenceStore` works as-is over POSIX `HalStorage` under `./fs_`.

Manual verification matrix in simulator:

| Verification | Steps | Pass criterion |
|---|---|---|
| Header clock renders | `pio run -e simulator -t upload`; navigate to Home | "HH:MM" appears top-left of header |
| Format toggle | Settings → Time format → 12h | Header shows `"H:MM AM/PM"` on next redraw |
| Show-clock toggle off | Settings → Show clock in header → off | Header left side empty |
| Timezone offset | Change to UTC+5 | Header shifts by 5 hours |
| Manual entry | Settings → Set time manually → enter 03:14 → OK | Header shows `03:14` after offset adjustment |
| Sync now error | Settings → Sync time now | Popup "Sync failed" (NTP stub) |
| Persistence | Set time, restart simulator, return to Home | Header shows the restored time, not host wall time |
| Corrupt persistence | Edit `./fs_/.crosspoint/time.bin` to garbage; restart | Header silently empty; settings still functional |

### 8.3 Layer 3 — Hardware verification on X4

1. Cold boot, Wi-Fi reachable: time to "header shows HH:MM after a redraw" ≤ ~10 s.
2. Cold boot, Wi-Fi unreachable: header silently empty. Settings → Sync now → "Sync failed". Set time → header shows it.
3. Persistence across cold boot: set manual time, full power off, power on with Wi-Fi off. Header shows the manual time (stale).
4. Sync-now race with manual entry: enter manual time, immediately Sync now. Manual sticks until Sync now finishes; explicit Sync overrides.
5. Long deep-sleep drift: sync, sleep 1 hour, wake. Drift < 1 minute (consistent with ESP32-C3 RTC spec).
6. Web UI from phone: open device IP, "Set to my browser's time" — header updates after next Home redraw.
7. Settings UI doesn't regress: scroll new Time category; Settings header unchanged (clock suppressed because `title != nullptr`).
8. Logging: `LOG_INF/DBG/ERR` for `RTC` and `NTP` tags appear at boot; no error spam in steady state.

### 8.4 Layer 4 — Hardware verification on X3

1. Cold boot, no Wi-Fi: header shows time immediately (DS3231 already had it). No NTP task spawned.
2. Fresh DS3231 / dead battery (sub-2025 epoch): header empty until "Sync now" or manual set. After Sync now, DS3231 holds time across power cycle.
3. DS3231 yanked / I2C dead: construction falls back to `HalRtcInternal`. Device behaves like X4. `LOG_ERR("RTC", "DS3231 unavailable, falling back to internal")`.
4. Manual set via on-device editor: writes to DS3231; verify by power-cycling — time persists with no NVS.

### 8.5 Acceptance gate

> On X4 with Wi-Fi: clock appears on Home within ~10 s of cold boot. On X3 with DS3231: clock appears immediately on cold boot. Both: clock survives cold boot via NVS fallback (X4) or DS3231 (X3). Manual entry and Sync-now both work. Settings toggles take effect on next redraw. No background Wi-Fi after the cold-boot sync attempt.

## 9. CHANGELOG entry (drafted)

```
### Added
- Header clock on the Home screen. Configurable in Settings (24h/12h, UTC offset).
- Cold-boot NTP sync on X4 (uses last-connected Wi-Fi, runs once, then disconnects).
- DS3231 hardware RTC support on X3 (read and write).
- Manual time entry in Settings (on-device and via the web UI).
```

## 10. Files inventory (single place to grep)

**New:**

- `lib/hal/HalRtc.h`
- `lib/hal/HalRtcDS3231.cpp`, `.h`
- `lib/hal/HalRtcInternal.cpp`, `.h`
- `src/services/TimeService.cpp`, `.h`
- `src/services/NtpSyncService.cpp`, `.h`
- `src/TimePersistenceStore.cpp`, `.h`
- `src/activities/settings/SetTimeActivity.cpp`, `.h`
- `test/time_format/test_time_format.cpp`
- `test/time_persistence/test_time_persistence.cpp`
- `test/run_time_tests.sh`

**Modified:**

- `src/CrossPointSettings.h`
- `src/SettingsList.h`
- `src/activities/settings/SettingsActivity.cpp`
- `src/components/themes/lyra/LyraTheme.cpp`
- `src/components/themes/BaseTheme.cpp`
- `src/main.cpp`
- `src/activities/reader/KOReaderSyncActivity.cpp` (NTP code extracted to `NtpSyncService`)
- `lib/I18n/translations/english.yaml`
- `src/network/html/SettingsPage.html` (and `SettingsPageHtml.generated.h` regenerated by `scripts/build_html.py`)
- `src/network/CrossPointWebServer.cpp`
- `CHANGELOG.md`
- `platformio.ini` (`lib_ignore` for simulator gains `HalRtcDS3231`)
