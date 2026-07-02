// lib/hal/HalRtcDS3231.cpp
#include "HalRtcDS3231.h"

#include <Wire.h>
#include <time.h>

#include "HalGPIO.h"
#include "Logging.h"

namespace {
constexpr uint8_t kAddr = I2C_ADDR_DS3231;   // 0x68
constexpr uint8_t kSecReg = DS3231_SEC_REG;  // 0x00

uint8_t bcdToBin(uint8_t v) { return uint8_t((v >> 4) * 10 + (v & 0x0F)); }
uint8_t binToBcd(uint8_t v) { return uint8_t(((v / 10) << 4) | (v % 10)); }

// Convert a struct tm in UTC to epoch seconds without depending on libc's
// TZ-environment state. Uses Howard Hinnant's civil-from-days algorithm.
// Valid for years in the range [-32767, 32767]; we only ever feed it 2000+.
int64_t tmToUtcEpoch(const tm& t) {
  int y = t.tm_year + 1900;
  unsigned m = unsigned(t.tm_mon + 1);
  unsigned d = unsigned(t.tm_mday);
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = unsigned(y - era * 400);                                 // [0, 399]
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;         // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                   // [0, 146096]
  const int64_t days = int64_t(era) * 146097 + int64_t(doe) - 719468;           // days since 1970-01-01
  return days * 86400 + int64_t(t.tm_hour) * 3600 + int64_t(t.tm_min) * 60 + int64_t(t.tm_sec);
}

// Returns true if Wire transactions succeed and seconds-register is plausible BCD.
bool probe() {
  Wire.beginTransmission(kAddr);
  Wire.write(kSecReg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(kAddr, uint8_t(1)) != 1) return false;
  uint8_t sec = Wire.read();
  uint8_t tens = (sec >> 4) & 0x07;
  uint8_t ones = sec & 0x0F;
  return tens <= 5 && ones <= 9;
}
}  // namespace

HalRtcDS3231::HalRtcDS3231() {
  Wire.begin(X3_I2C_SDA, X3_I2C_SCL, X3_I2C_FREQ);
  Wire.setTimeOut(6);
  available_ = probe();
  if (!available_) {
    LOG_ERR("RTC", "DS3231 probe failed");
  }
}

bool HalRtcDS3231::readUtcEpoch(int64_t* out) {
  if (!available_ || out == nullptr) return false;
  Wire.beginTransmission(kAddr);
  Wire.write(kSecReg);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(kAddr, uint8_t(7)) != 7) return false;

  uint8_t b[7];
  for (int i = 0; i < 7; ++i) b[i] = Wire.read();

  tm t{};
  t.tm_sec = bcdToBin(b[0] & 0x7F);
  t.tm_min = bcdToBin(b[1] & 0x7F);
  t.tm_hour = bcdToBin(b[2] & 0x3F);  // assumes 24h mode (bit 6 = 0)
  // b[3] = day of week (1..7) — ignored
  t.tm_mday = bcdToBin(b[4] & 0x3F);
  uint8_t monthRaw = b[5];
  t.tm_mon = bcdToBin(monthRaw & 0x1F) - 1;  // tm_mon is 0..11
  int year = bcdToBin(b[6]) + 2000;
  if (monthRaw & 0x80) year += 100;  // century bit (we'll never see this in practice)
  t.tm_year = year - 1900;

  // Convert UTC tm -> epoch without touching libc TZ state. ESP-IDF's newlib
  // variant on this toolchain doesn't expose timegm, and mktime would apply
  // the configured local-time offset.
  *out = tmToUtcEpoch(t);
  return *out >= kMinValidEpoch;
}

bool HalRtcDS3231::writeUtcEpoch(int64_t utcEpoch) {
  if (!available_) return false;
  time_t epoch = time_t(utcEpoch);
  tm t{};
  gmtime_r(&epoch, &t);

  // Century bit: set when year >= 2100. Won't happen in practice.
  uint8_t monthByte = binToBcd(uint8_t(t.tm_mon + 1));
  if (t.tm_year + 1900 >= 2100) monthByte |= 0x80;

  Wire.beginTransmission(kAddr);
  Wire.write(kSecReg);
  Wire.write(binToBcd(uint8_t(t.tm_sec)));
  Wire.write(binToBcd(uint8_t(t.tm_min)));
  Wire.write(binToBcd(uint8_t(t.tm_hour)));  // 24h mode (bit 6 = 0)
  Wire.write(uint8_t(t.tm_wday + 1));        // 1..7
  Wire.write(binToBcd(uint8_t(t.tm_mday)));
  Wire.write(monthByte);
  Wire.write(binToBcd(uint8_t((t.tm_year + 1900) % 100)));
  if (Wire.endTransmission() != 0) {
    LOG_ERR("RTC", "DS3231 write failed");
    return false;
  }
  return true;
}

bool HalRtcDS3231::hasValidTime() {
  int64_t epoch;
  return readUtcEpoch(&epoch);
}
