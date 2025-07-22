#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include "GLOBAL_DEFINES.h"
#include <TimeLib.h>

// For NTP
#include <WiFi.h>
#include "NTPClient.h"

#include "StoredConfig.h"
// For TFTs::blanked
#include "TFTs.h"

class Clock
{
public:
  Clock() : loop_time(0), local_time(0), time_valid(false), config(NULL) {}

  // The global WiFi from WiFi.h must already be .begin()'d before calling Clock::begin()
  void begin(StoredConfig::Config::Clock *config_);
  void loop();

  // Calls NTPClient::getEpochTime() or RTC::get() as appropriate
  // This has to be static to pass to TimeLib::setSyncProvider.
  static time_t syncProvider();

  // Adaptive NTP sync methods
  static void handleNtpSuccess();
  static void handleNtpFailure();
  static void updateNtpInterval();
  static uint32_t getCurrentNtpInterval() { return current_ntp_interval_ms; }

  // Set preferred hour format. true = 12hr, false = 24hr
  void setTwelveHour(bool th) { config->twelve_hour = th; }
  bool getTwelveHour() { return config->twelve_hour; }
  void toggleTwelveHour() { config->twelve_hour = !config->twelve_hour; }
  // Blanked: 1:23   Not blanked: 01:23
  void setBlankHoursZero(bool bhz) { config->blank_hours_zero = bhz; }
  bool getBlankHoursZero() { return config->blank_hours_zero; }
  void toggleBlankHoursZero() { config->blank_hours_zero = !config->blank_hours_zero; }

  // Internal time is kept in UTC. This affects the displayed time.
  void setTimeZoneOffset(time_t offset) { config->time_zone_offset = offset; }
  time_t getTimeZoneOffset() { return config->time_zone_offset; }
  void adjustTimeZoneOffset(time_t adj) { config->time_zone_offset += adj; }
  void setActiveGraphicIdx(int8_t idx) { config->selected_graphic = idx; }
  int8_t getActiveGraphicIdx() { return config->selected_graphic; }
  void adjustClockGraphicsIdx(int8_t adj)
  {
    int8_t newGraphic = getActiveGraphicIdx();
    newGraphic += adj;

    if (newGraphic > tfts.NumberOfClockFaces)
    {
      newGraphic = 1;
    }
    if (newGraphic < 1)
    {
      newGraphic = tfts.NumberOfClockFaces;
    }

    this->setClockGraphicsIdx(newGraphic);
  }
  void setClockGraphicsIdx(int8_t set)
  {
    if (set > tfts.NumberOfClockFaces)
    {
      set = tfts.NumberOfClockFaces;
    }
    if (set < 1)
    {
      set = 1;
    }

    config->selected_graphic = set;
  }

  // Proxy C functions from TimeLib.h
  // I really wish it were a class we could just subclass, but here we are.
  uint16_t getYear() { return year(local_time); }
  uint8_t getMonth() { return month(local_time); }
  uint8_t getDay() { return day(local_time); }
  uint8_t getHour() { return config->twelve_hour ? hourFormat12(local_time) : hour(local_time); }
  uint8_t getHour12() { return hourFormat12(local_time); }
  uint8_t getHour24() { return hour(local_time); }
  uint8_t getMinute() { return minute(local_time); }
  uint8_t getSecond() { return second(local_time); }
  bool isAm() { return isAM(local_time); }
  bool isPm() { return isPM(local_time); }

  // Helper functions for making a clock.
  uint8_t getHoursTens();
  uint8_t getHoursOnes() { return getHour() % 10; }
  uint8_t getHours12Tens() { return getHour12() / 10; }
  uint8_t getHours12Ones() { return getHour12() % 10; }
  uint8_t getHours24Tens() { return getHour24() / 10; }
  uint8_t getHours24Ones() { return getHour24() % 10; }
  uint8_t getMinutesTens() { return getMinute() / 10; }
  uint8_t getMinutesOnes() { return getMinute() % 10; }
  uint8_t getSecondsTens() { return getSecond() / 10; }
  uint8_t getSecondsOnes() { return getSecond() % 10; }

  time_t loop_time, local_time;

private:
  bool time_valid;
  StoredConfig::Config::Clock *config;

  // Static variables needed for syncProvider()
  static WiFiUDP ntpUDP;
  static NTPClient ntpTimeClient;
  static uint32_t millis_last_ntp;

  // Adaptive NTP sync intervals
  static uint32_t current_ntp_interval_ms;
  static uint8_t consecutive_failures;
  static uint8_t consecutive_successes;

  // NTP interval constants (in milliseconds)
  const static uint32_t ntp_interval_initial_ms = 300000; // 5 min - initial sync after boot
  const static uint32_t ntp_interval_normal_ms = 1800000; // 30 min - normal operation
  const static uint32_t ntp_interval_stable_ms = 3600000; // 1 hour - stable operation
  const static uint32_t ntp_interval_error_ms = 600000;   // 10 min - after errors
  const static uint32_t ntp_interval_max_ms = 7200000;    // 2 hours - maximum interval
};

extern Clock uclock;

#endif // CLOCK_H
