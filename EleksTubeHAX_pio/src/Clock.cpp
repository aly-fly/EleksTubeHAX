#include "Clock.h"
#include "WiFi_WPS.h"

#if defined(HARDWARE_SI_HAI_CLOCK) || defined(HARDWARE_IPSTUBE_CLOCK) // for Clocks with DS1302 chip (SI HAI or IPSTUBE)
// If it is a SI HAI Clock, use differnt RTC chip drivers
#include <ThreeWire.h>
#include <RtcDS1302.h>
ThreeWire myWire(DS1302_IO, DS1302_SCLK, DS1302_CE); // IO, SCLK, CE
RtcDS1302<ThreeWire> RTC(myWire);
void RtcBegin()
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: Tryng to call RTC.Begin()");
#endif
  RTC.Begin();
  if (!RTC.IsDateTimeValid())
  {
    // Common Causes for this to be true:
    //    1) the entire RTC was just set to the default date (01/01/2000 00:00:00)
    //    2) first time you ran and the device wasn't running yet
    //    3) the battery on the device is low or even missing
    Serial.println("RTC lost confidence in the DateTime!");
  }
  if (RTC.GetIsWriteProtected())
  {
    Serial.println("RTC was write protected, enabling writing now");
    RTC.SetIsWriteProtected(false);
  }

  if (!RTC.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    RTC.SetIsRunning(true);
  }
}

uint32_t RtcGet()
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: RtcGet() for DS1302 RTC entered.");
  Serial.println("DEBUG_OUTPUT_RTC: Calling RTC.GetDateTime()...");
#endif
  RtcDateTime temptime;
  temptime = RTC.GetDateTime();
  uint32_t returnvalue = temptime.Unix32Time();
#ifdef DEBUG_OUTPUT_RTC
  Serial.print("DEBUG_OUTPUT_RTC: RTC.GetDateTime() returned: ");
  Serial.println(returnvalue);
#endif
  return returnvalue;
}

void RtcSet(uint32_t tt)
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: RtcSet() for DS1302 RTC entered.");
  Serial.print("DEBUG_OUTPUT_RTC: Setting DS1302 RTC to: ");
  Serial.println(tt);
#endif
  RtcDateTime temptime;
  temptime.InitWithUnix32Time(tt);
#ifdef DEBUG_OUTPUT
  Serial.println("DEBUG_OUTPUT_RTC: DS1302 RTC time set.");
#endif
  RTC.SetDateTime(temptime);
}
#elif defined(HARDWARE_NovelLife_SE_CLOCK) // for NovelLife_SE clone with R8025T RTC chip
#include <RTC_RX8025T.h>                   // This header will now use Wire1 for I2C operations.

RX8025T RTC;

void RtcBegin()
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("");
  Serial.println("DEBUG_OUTPUT_RTC: Trying to call RTC.Init()");
#endif
  RTC.init((uint32_t)RTC_SDA_PIN, (uint32_t)RTC_SCL_PIN); // setup second I2C for the RX8025T RTC chip
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: RTC RX8025T initialized!");
#endif
  delay(100);
  return;
}

void RtcSet(uint32_t tt)
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.print("DEBUG_OUTPUT_RTC: Setting RX8025T RTC to: ");
  Serial.println(tt);
#endif

  int ret = RTC.set(tt);
  if (ret != 0)
  {
    Serial.print("Error setting RX8025T RTC: ");
    Serial.println(ret);
  }
  else
  {
#ifdef DEBUG_OUTPUT_RTC
    Serial.println("DEBUG_OUTPUT_RTC: RX8025T RTC time set successfully!");
#endif
  }
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: RX8025T RTC time set.");
#endif
}

uint32_t RtcGet()
{
  uint32_t returnvalue = RTC.get(); // Get the RTC time
#ifdef DEBUG_OUTPUT_RTC
  Serial.print("DEBUG_OUTPUT_RTC: RtcGet() RX8025T returned: ");
  Serial.println(returnvalue);
#endif
  return returnvalue;
}
#else // for Elekstube and all other clocks with DS3231 RTC chip or DS1307/PCF8523
#include <RTClib.h>

RTC_DS3231 RTC; // DS3231, works also with DS1307 or PCF8523

void RtcBegin()
{
  if (!RTC.begin())
  {
    Serial.println("NO supported RTC found!");
  }
  else
  {
#ifdef DEBUG_OUTPUT_RTC
    Serial.println("DEBUG_OUTPUT_RTC: RTC found!");
#endif
  }

  // check if the RTC chip reports a power failure
  bool bPowerLost = 0;
  bPowerLost = RTC.lostPower();
  if (bPowerLost)
  {
    Serial.println("RTC reports power was lost! Setting time to default value.");
    RTC.adjust(DateTime(2023, 1, 1, 0, 0, 0)); // set the RTC time to a default value
  }
}

uint32_t RtcGet()
{
  DateTime now = RTC.now(); // convert to unix time
  uint32_t returnvalue = now.unixtime();
#ifdef DEBUG_OUTPUT_RTC
  Serial.print("DEBUG_OUTPUT_RTC: DS3231 RTC now.unixtime() = ");
  Serial.println(returnvalue);
#endif
  return returnvalue;
}

void RtcSet(uint32_t tt)
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.print("DEBUG_OUTPUT_RTC: Attempting to set DS3231 RTC to: ");
  Serial.println(tt);
#endif

  DateTime timetoset(tt); // convert to unix time
  RTC.adjust(timetoset);  // set the RTC time
#ifdef DEBUG_OUTPUT
  Serial.println("DEBUG_OUTPUT_RTC: DS3231 RTC time updated.");
#endif
}
#endif // end of RTC chip selection

void Clock::begin(StoredConfig::Config::Clock *config_)
{
  config = config_;

  if (config->is_valid != StoredConfig::valid)
  {
    // Config is invalid, probably a new device never had its config written.
    // Load some reasonable defaults.
    Serial.println("Loaded Clock config is invalid, using default.  This is normal on first boot.");
    setTwelveHour(false);
    setBlankHoursZero(false);
    setTimeZoneOffset(1 * 3600); // CET
    setActiveGraphicIdx(1);
    config->is_valid = StoredConfig::valid;
  }

  RtcBegin();
  ntpTimeClient.begin();
  ntpTimeClient.update();
  Serial.print("NTP time = ");
  Serial.println(ntpTimeClient.getFormattedTime());
  setSyncProvider(&Clock::syncProvider);
}

void Clock::loop()
{
  if (timeStatus() == timeNotSet)
  {
    time_valid = false;
  }
  else
  {
    loop_time = now();
    local_time = loop_time + config->time_zone_offset;
    time_valid = true;
  }
}

// Static methods used for sync provider to TimeLib library.
time_t Clock::syncProvider()
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("Clock:syncProvider() entered.");
#endif
  time_t rtc_now;
  rtc_now = RtcGet(); // Get the RTC time

  if (millis() - millis_last_ntp > refresh_ntp_every_ms || millis_last_ntp == 0) // Get NTP time only every 10 minutes or if not yet done
  {                                                                              // It's time to get a new NTP sync
    if (WifiState == connected)
    { // We have WiFi, so try to get NTP time.
      Serial.print("Try to get the actual time from NTP server...");
      if (ntpTimeClient.update())
      {
        Serial.print(".");
        time_t ntp_now = ntpTimeClient.getEpochTime();
        Serial.println("NTP query done.");
        Serial.print("NTP time = ");
        Serial.println(ntpTimeClient.getFormattedTime());
        rtc_now = RtcGet(); // Get the RTC time again, because it may have changed in the meantime
        // Sync the RTC to NTP if needed.
        Serial.print("NTP  :");
        Serial.println(ntp_now);
        Serial.print("RTC  :");
        Serial.println(rtc_now);
        Serial.print("Diff: ");
        Serial.println(ntp_now - rtc_now);
        if ((ntp_now != rtc_now) && (ntp_now > 1743364444)) // check if we have a difference and a valid NTP time
        {                                                   // NTP time is valid and different from RTC time
          Serial.println("RTC time is not valid, updating RTC.");
          RtcSet(ntp_now);
          Serial.println("RTC is now set to NTP time.");
          rtc_now = RtcGet(); // Check if RTC time is set correctly
          Serial.print("RTC time = ");
          Serial.println(rtc_now);
        }
        else if ((ntp_now != rtc_now) && (ntp_now < 1743364444))
        { // NTP can't be valid!
          Serial.println("Time returned from NTP is not valid! Using RTC time!");
          rtc_now = RtcGet(); // Get the RTC time again, because it may have changed in the meantime
          return rtc_now;
        }
        millis_last_ntp = millis(); // store the last time we tried to get NTP time

        Serial.println("Using NTP time!");
        return ntp_now;
      }
      else
      {                     // NTP return value is not valid
        rtc_now = RtcGet(); // Get the RTC time again, because it may have changed in the meantime
        Serial.println("Invalid NTP response, using RTC time.");
        return rtc_now;
      }
    } // no WiFi!
    Serial.println("No WiFi, using RTC time.");
    return rtc_now;
  }
  Serial.println("Using RTC time.");
  return rtc_now;
}

uint8_t Clock::getHoursTens()
{
  uint8_t hour_tens = getHour() / 10;

  if (config->blank_hours_zero && hour_tens == 0)
  {
    return TFTs::blanked;
  }
  else
  {
    return hour_tens;
  }
}

uint32_t Clock::millis_last_ntp = 0;
WiFiUDP Clock::ntpUDP;
NTPClient Clock::ntpTimeClient(ntpUDP);
