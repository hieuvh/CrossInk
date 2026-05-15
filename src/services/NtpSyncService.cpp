#include "NtpSyncService.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "Logging.h"
#include "WifiCredentialStore.h"
// Sanity bounds for the synced epoch — defined in lib/hal/HalRtc.h (Task 4 moved them there).
#include "../../lib/hal/HalRtc.h"

NtpSyncService& NtpSyncService::instance() {
  static NtpSyncService s;
  return s;
}

namespace {
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

NtpSyncService::Result NtpSyncService::syncOnce(uint32_t wifiTimeoutMs, uint32_t ntpTimeoutMs) {
  resetCancel();

  const WifiCredential* cred = pickCredential();
  if (cred == nullptr) {
    LOG_INF("NTP", "no saved Wi-Fi credentials");
    return {false, 0, Error::NoCredentials};
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(cred->ssid.c_str(), cred->password.c_str());

  uint32_t waited = 0;
  while (WiFi.status() != WL_CONNECTED && waited < wifiTimeoutMs) {
    if (cancelFlag_.load()) {
      wifiOff();
      return {false, 0, Error::WifiConnectFailed};
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    waited += 100;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("NTP", "Wi-Fi connect failed (%s)", cred->ssid.c_str());
    wifiOff();
    return {false, 0, Error::WifiConnectFailed};
  }

  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  waited = 0;
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && waited < ntpTimeoutMs) {
    if (cancelFlag_.load()) {
      wifiOff();
      return {false, 0, Error::NtpTimeout};
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
    waited += 100;
  }

  if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    LOG_ERR("NTP", "SNTP timeout");
    wifiOff();
    return {false, 0, Error::NtpTimeout};
  }

  timeval tv;
  gettimeofday(&tv, nullptr);
  int64_t epoch = int64_t(tv.tv_sec);
  wifiOff();

  if (epoch < kMinValidEpoch || epoch > kMaxValidEpoch) {
    LOG_ERR("NTP", "absurd epoch %lld", static_cast<long long>(epoch));
    return {false, 0, Error::BadEpoch};
  }
  LOG_INF("NTP", "synced epoch=%lld", static_cast<long long>(epoch));
  return {true, epoch, Error::None};
}
