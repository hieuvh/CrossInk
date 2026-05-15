#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class RtcBackend;
class TimePersistenceStore;

enum class TimeSource : uint8_t { None, RestoredFromNvs, NtpSynced, ManuallySet };

class TimeService {
 public:
  enum class DeviceType : uint8_t { X4 = 0, X3 = 1 };

  static TimeService& instance();

  // One-time setup. Selects backend, restores from NVS if needed, optionally spawns
  // the cold-boot NTP task.
  void boot(DeviceType deviceType);

  // Header read path. Returns true on success and writes a NUL-terminated string.
  bool formatLocal(char* out, size_t cap);

  bool hasValidTime();

  // Called from cold-boot NTP task and from "Sync now" popup.
  // boot-task path passes ignoreManualGuard=false; sync-now passes true.
  void onNtpSynced(int64_t epoch, bool ignoreManualGuard);

  // Called from SetTimeActivity OK and from POST /api/time handler.
  void onManualSet(int64_t epoch);

  TimeSource currentSource();

 private:
  TimeService() = default;
  ~TimeService() = default;

  std::unique_ptr<RtcBackend> rtc_;
  std::unique_ptr<TimePersistenceStore> persistence_;
  TimeSource source_ = TimeSource::None;
  SemaphoreHandle_t mutex_ = nullptr;

  static void coldBootNtpTask(void* arg);
};
