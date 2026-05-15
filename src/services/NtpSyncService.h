#pragma once
#include <atomic>
#include <cstdint>

class NtpSyncService {
 public:
  enum class Error : uint8_t { None, NoCredentials, WifiConnectFailed, NtpTimeout, BadEpoch };

  struct Result {
    bool ok;
    int64_t epoch;  // valid only if ok
    Error error;    // None if ok
  };

  static NtpSyncService& instance();

  // Connect to lastConnectedSsid (or first credential), run SNTP, return result.
  // Disconnects Wi-Fi before returning regardless of success.
  // wifiTimeoutMs: how long to wait for WL_CONNECTED before giving up.
  // ntpTimeoutMs: how long to wait for SNTP_SYNC_STATUS_COMPLETED before giving up.
  Result syncOnce(uint32_t wifiTimeoutMs = 8000, uint32_t ntpTimeoutMs = 5000);

  // Cooperative cancel: poll loop checks this each ~100 ms tick.
  void cancel() { cancelFlag_.store(true); }
  void resetCancel() { cancelFlag_.store(false); }

 private:
  NtpSyncService() = default;
  std::atomic<bool> cancelFlag_{false};
};
