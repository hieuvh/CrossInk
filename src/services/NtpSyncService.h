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
  // wifiTimeoutMs: how long to wait for WL_CONNECTED before giving up.
  //                Default 15000ms, matches WifiSelectionActivity's CONNECTION_TIMEOUT_MS.
  // ntpTimeoutMs: how long to wait for the SNTP sync callback to fire.
  //               20s headroom for DNS + UDP round-trip across multiple servers
  //               (pool.ntp.org / time.google.com / time.cloudflare.com).
  // tearDownWifi: when true (default) Wi-Fi is disconnected before returning on every
  //   exit path (cold-boot task, "Sync now" popup). Callers that need Wi-Fi to remain
  //   up for follow-on requests (e.g. KOReader sync) pass false.
  Result syncOnce(uint32_t wifiTimeoutMs = 15000, uint32_t ntpTimeoutMs = 20000, bool tearDownWifi = true);

  // Cooperative cancel: poll loop checks this each ~100 ms tick.
  void cancel() { cancelFlag_.store(true); }
  void resetCancel() { cancelFlag_.store(false); }

 private:
  NtpSyncService() = default;
  std::atomic<bool> cancelFlag_{false};
};
