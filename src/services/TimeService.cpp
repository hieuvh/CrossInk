#include "TimeService.h"

#include <HalStorage.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "Logging.h"
#include "TimeFormat.h"
#include "TimePersistenceStore.h"
#include "WifiCredentialStore.h"
#include "../../lib/hal/HalRtc.h"
#ifndef SIMULATOR
#include "../../lib/hal/HalRtcDS3231.h"
#include "../../lib/hal/HalRtcInternal.h"
#endif
#include "NtpSyncService.h"

#ifdef SIMULATOR
#include <sys/time.h>
#endif

namespace {

// Adapter: TimePersistenceStore::Filesystem -> HalStorage.
class HalStorageFs : public TimePersistenceStore::Filesystem {
 public:
  bool exists(const char* p) override { return Storage.exists(p); }
  bool remove(const char* p) override { return Storage.remove(p); }
  bool rename(const char* o, const char* n) override { return Storage.rename(o, n); }
  bool read(const char* p, uint8_t* out, size_t cap, size_t* outLen) override {
    FsFile f;
    if (!Storage.openFileForRead("TPS", p, f)) return false;
    int n = f.read(out, cap);
    f.close();
    if (n <= 0) return false;
    *outLen = static_cast<size_t>(n);
    return true;
  }
  bool write(const char* p, const uint8_t* data, size_t len) override {
    Storage.mkdir("/.crosspoint");
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FsFile f;
    if (!Storage.openFileForWrite("TPS", tmp, f)) return false;
    size_t w = f.write(data, len);
    f.flush();
    f.close();
    if (w != len) {
      Storage.remove(tmp);
      return false;
    }
    if (Storage.exists(p)) Storage.remove(p);
    return Storage.rename(tmp, p);
  }
};

HalStorageFs& fs() {
  static HalStorageFs s;
  return s;
}

#ifdef SIMULATOR
class Lock {
 public:
  explicit Lock(void* /*m*/) {}
};

// Simulator RTC backend: tracks an in-process offset from the host wall clock.
// writeUtcEpoch is observable in tests, and after boot it defaults to host time
// so the simulator shows a sensible clock without manual setup.
class SimRtcBackend : public RtcBackend {
 public:
  bool readUtcEpoch(int64_t* out) override {
    if (out == nullptr) return false;
    if (!valid_) return false;
    timeval tv;
    gettimeofday(&tv, nullptr);
    *out = epochAtSet_ + (int64_t(tv.tv_sec) - hostAtSet_);
    return *out >= kMinValidEpoch;
  }
  bool writeUtcEpoch(int64_t epoch) override {
    timeval tv;
    gettimeofday(&tv, nullptr);
    epochAtSet_ = epoch;
    hostAtSet_ = int64_t(tv.tv_sec);
    valid_ = true;
    return true;
  }
  bool hasValidTime() override {
    int64_t e;
    return readUtcEpoch(&e);
  }

 private:
  bool valid_ = false;
  int64_t epochAtSet_ = 0;
  int64_t hostAtSet_ = 0;
};
#else
class Lock {
 public:
  explicit Lock(SemaphoreHandle_t m) : m_(m) {
    if (m_) xSemaphoreTake(m_, portMAX_DELAY);
  }
  ~Lock() {
    if (m_) xSemaphoreGive(m_);
  }

 private:
  SemaphoreHandle_t m_;
};
#endif

}  // namespace

TimeService& TimeService::instance() {
  static TimeService s;
  return s;
}

void TimeService::boot(DeviceType deviceType) {
#ifndef SIMULATOR
  if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
#endif

#ifdef SIMULATOR
  (void)deviceType;
  rtc_ = std::make_unique<SimRtcBackend>();
  // Default to host wall time so the simulator shows something sensible without manual setup.
  timeval tv;
  gettimeofday(&tv, nullptr);
  rtc_->writeUtcEpoch(int64_t(tv.tv_sec));
#else
  if (deviceType == DeviceType::X3) {
    auto ds = std::make_unique<HalRtcDS3231>();
    if (ds->isAvailable()) {
      rtc_ = std::move(ds);
    } else {
      LOG_ERR("RTC", "DS3231 unavailable, falling back to internal");
      rtc_ = std::make_unique<HalRtcInternal>();
    }
  } else {
    rtc_ = std::make_unique<HalRtcInternal>();
  }
#endif

  persistence_ = std::make_unique<TimePersistenceStore>(fs());
  persistence_->load();

  if (rtc_->hasValidTime()) {
#ifdef SIMULATOR
    source_ = TimeSource::RestoredFromNvs;
#else
    source_ = (deviceType == DeviceType::X3) ? TimeSource::NtpSynced : TimeSource::RestoredFromNvs;
#endif
  } else if (persistence_->hasLastSynced()) {
    rtc_->writeUtcEpoch(persistence_->lastSyncedUtc());
    source_ = TimeSource::RestoredFromNvs;
  } else {
    source_ = TimeSource::None;
  }

#ifndef SIMULATOR
  // Spawn cold-boot NTP task if needed. X4 always tries (no battery-backed RTC);
  // X3 only tries when its DS3231 is uninitialized.
  const bool needsBootNtp =
      (deviceType == DeviceType::X4) ||
      (deviceType == DeviceType::X3 && !rtc_->hasValidTime());
  if (needsBootNtp && !WifiCredentialStore::getInstance().getCredentials().empty()) {
    xTaskCreate(&TimeService::coldBootNtpTask, "ntp_boot", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr);
  }
#endif
}

void TimeService::coldBootNtpTask(void* /*arg*/) {
#ifndef SIMULATOR
  auto result = NtpSyncService::instance().syncOnce();
  if (result.ok) {
    TimeService::instance().onNtpSynced(result.epoch, /*ignoreManualGuard=*/false);
  }
  vTaskDelete(nullptr);
#endif
}

bool TimeService::formatLocal(char* out, size_t cap) {
  Lock g(mutex_);
  if (rtc_ == nullptr) return false;
  int64_t epoch;
  if (!rtc_->readUtcEpoch(&epoch)) return false;
  const int offset = static_cast<int>(SETTINGS.utcOffsetIndex) - 12;  // 0..26 -> -12..+14
  return TimeFormat::format(epoch, offset, SETTINGS.timeFormat == 0, out, cap);
}

bool TimeService::hasValidTime() {
  Lock g(mutex_);
  return rtc_ != nullptr && rtc_->hasValidTime();
}

void TimeService::onNtpSynced(int64_t epoch, bool ignoreManualGuard) {
  Lock g(mutex_);
  if (!ignoreManualGuard && source_ == TimeSource::ManuallySet) return;
  if (rtc_ == nullptr) return;
  rtc_->writeUtcEpoch(epoch);
  if (persistence_) persistence_->write(epoch);
  source_ = TimeSource::NtpSynced;
}

void TimeService::onManualSet(int64_t epoch) {
  Lock g(mutex_);
  if (rtc_ == nullptr) return;
  rtc_->writeUtcEpoch(epoch);
  if (persistence_) persistence_->write(epoch);
  source_ = TimeSource::ManuallySet;
}

TimeSource TimeService::currentSource() {
  Lock g(mutex_);
  return source_;
}
