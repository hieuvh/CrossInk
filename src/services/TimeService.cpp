#include "TimeService.h"

#include <HalStorage.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "Logging.h"
#include "TimeFormat.h"
#include "TimePersistenceStore.h"
#include "WifiCredentialStore.h"
#include "../../lib/hal/HalRtc.h"
#include "../../lib/hal/HalRtcDS3231.h"
#include "../../lib/hal/HalRtcInternal.h"
#include "NtpSyncService.h"

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

}  // namespace

TimeService& TimeService::instance() {
  static TimeService s;
  return s;
}

void TimeService::boot(DeviceType deviceType) {
  if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();

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

  persistence_ = std::make_unique<TimePersistenceStore>(fs());
  persistence_->load();

  if (rtc_->hasValidTime()) {
    source_ = (deviceType == DeviceType::X3) ? TimeSource::NtpSynced : TimeSource::RestoredFromNvs;
  } else if (persistence_->hasLastSynced()) {
    rtc_->writeUtcEpoch(persistence_->lastSyncedUtc());
    source_ = TimeSource::RestoredFromNvs;
  } else {
    source_ = TimeSource::None;
  }

  // Spawn cold-boot NTP task if needed. X4 always tries (no battery-backed RTC);
  // X3 only tries when its DS3231 is uninitialized.
  const bool needsBootNtp =
      (deviceType == DeviceType::X4) ||
      (deviceType == DeviceType::X3 && !rtc_->hasValidTime());
  if (needsBootNtp && !WifiCredentialStore::getInstance().getCredentials().empty()) {
    xTaskCreate(&TimeService::coldBootNtpTask, "ntp_boot", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr);
  }
}

void TimeService::coldBootNtpTask(void* /*arg*/) {
  auto result = NtpSyncService::instance().syncOnce();
  if (result.ok) {
    TimeService::instance().onNtpSynced(result.epoch, /*ignoreManualGuard=*/false);
  }
  vTaskDelete(nullptr);
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
