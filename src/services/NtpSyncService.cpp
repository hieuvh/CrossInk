#include "NtpSyncService.h"

#ifndef SIMULATOR

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include <atomic>

#include "Logging.h"
#include "WifiCredentialStore.h"
// Sanity bounds for the synced epoch — defined in lib/hal/HalRtc.h (Task 4 moved them there).
#include "../../lib/hal/HalRtc.h"

NtpSyncService& NtpSyncService::instance() {
  static NtpSyncService s;
  return s;
}

namespace {
// SNTP fires this callback exactly once per successful sync. Polling
// sntp_get_sync_status() races with the SDK clearing the flag back to RESET,
// so we use the callback to latch a flag we can poll without losing the edge.
std::atomic<bool> g_sntpSynced{false};
void sntpSyncedCallback(struct timeval* /*tv*/) { g_sntpSynced.store(true); }

const WifiCredential* pickCredential() {
  auto& store = WifiCredentialStore::getInstance();
  const auto& creds = store.getCredentials();
  if (creds.empty()) return nullptr;
  const std::string& last = store.getLastConnectedSsid();
  if (!last.empty()) {
    if (auto* c = store.findCredential(last)) return c;
  }
  return &creds.front();
}

void wifiOff() {
  if (esp_sntp_enabled()) esp_sntp_stop();
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}
}  // namespace

NtpSyncService::Result NtpSyncService::syncOnce(uint32_t wifiTimeoutMs, uint32_t ntpTimeoutMs, bool tearDownWifi) {
  resetCancel();

  const WifiCredential* cred = pickCredential();
  if (cred == nullptr) {
    LOG_INF("NTP", "no saved Wi-Fi credentials");
    return {false, 0, Error::NoCredentials};
  }

  // Match WifiSelectionActivity's connect setup so we behave the same as the
  // existing Wi-Fi screen (which is known to work). Without these the SDK can
  // race against an in-flight auto-reconnect using stale NVS credentials and
  // never actually try the SSID we passed.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);

  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String hostname = "CrossPoint-Reader-" + mac;
  WiFi.setHostname(hostname.c_str());

  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());

  uint32_t waited = 0;
  while (WiFi.status() != WL_CONNECTED && waited < wifiTimeoutMs) {
    if (cancelFlag_.load()) {
      if (tearDownWifi) wifiOff();
      return {false, 0, Error::WifiConnectFailed};
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    waited += 100;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("NTP", "Wi-Fi connect failed (%s) status=%d", cred->ssid.c_str(), int(WiFi.status()));
    if (tearDownWifi) wifiOff();
    return {false, 0, Error::WifiConnectFailed};
  }

  IPAddress ip = WiFi.localIP();
  LOG_INF("NTP", "Wi-Fi up (%s) ip=%d.%d.%d.%d, starting SNTP", cred->ssid.c_str(), ip[0], ip[1], ip[2], ip[3]);

  if (esp_sntp_enabled()) esp_sntp_stop();
  g_sntpSynced.store(false);
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  // Multiple servers so a slow/unreachable pool.ntp.org rotation isn't a
  // single point of failure. SNTP tries them in order on each poll cycle.
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_setservername(2, "time.cloudflare.com");
  sntp_set_time_sync_notification_cb(sntpSyncedCallback);
  esp_sntp_init();

  waited = 0;
  while (!g_sntpSynced.load() && waited < ntpTimeoutMs) {
    if (cancelFlag_.load()) {
      if (tearDownWifi) wifiOff();
      return {false, 0, Error::NtpTimeout};
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    waited += 100;
  }

  if (!g_sntpSynced.load()) {
    LOG_ERR("NTP", "SNTP timeout after %lums (sync_status=%d, server0=%s reach=%u)",
            static_cast<unsigned long>(waited), int(sntp_get_sync_status()),
            esp_sntp_getservername(0) ? esp_sntp_getservername(0) : "?", unsigned(esp_sntp_getreachability(0)));
    if (tearDownWifi) wifiOff();
    return {false, 0, Error::NtpTimeout};
  }

  timeval tv;
  gettimeofday(&tv, nullptr);
  int64_t epoch = int64_t(tv.tv_sec);
  if (tearDownWifi) wifiOff();

  if (epoch < kMinValidEpoch || epoch > kMaxValidEpoch) {
    LOG_ERR("NTP", "absurd epoch %lld", static_cast<long long>(epoch));
    return {false, 0, Error::BadEpoch};
  }
  LOG_INF("NTP", "synced epoch=%lld", static_cast<long long>(epoch));
  return {true, epoch, Error::None};
}

#else  // SIMULATOR

NtpSyncService& NtpSyncService::instance() {
  static NtpSyncService s;
  return s;
}

NtpSyncService::Result NtpSyncService::syncOnce(uint32_t /*wifiTimeoutMs*/, uint32_t /*ntpTimeoutMs*/,
                                                bool /*tearDownWifi*/) {
  // Simulator stub: no Wi-Fi stack, always reports failure (matches the spec's
  // "expected stub failures" model). Use SimRtcBackend's writeUtcEpoch directly
  // for tests that need to inject time.
  return {false, 0, Error::WifiConnectFailed};
}

#endif  // SIMULATOR
