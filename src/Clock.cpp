#include "Clock.h"
#include "WiFi_WPS.h"

//-----------------------------------------------------------------------------------------------
// begin RTC chip stuff
//-----------------------------------------------------------------------------------------------

#if defined(HARDWARE_SI_HAI_CLOCK) || defined(HARDWARE_IPSTUBE_CLOCK) // For Clocks with DS1302 chip (Si Hai or IPSTube)
#include <ThreeWire.h>
#include <RtcDS1302.h>
ThreeWire myWire(DS1302_IO, DS1302_SCLK, DS1302_CE); // IO, SCLK, CE
RtcDS1302<ThreeWire> RTC(myWire);
void RtcBegin()
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: Tryng to call DS1302 RTC.Begin()");
#endif
  RTC.Begin();
  if (!RTC.IsDateTimeValid())
  {
    // Common Causes for this to be true:
    //    1) the entire RTC was just set to the default date (01/01/2000 00:00:00)
    //    2) first time you ran and the device wasn't running yet
    //    3) the battery on the device is low or even missing
    Serial.println("DS1302 RTC lost confidence in the DateTime!");
  }
  if (RTC.GetIsWriteProtected())
  {
    Serial.println("DS1302 RTC was write protected, enabling writing now");
    RTC.SetIsWriteProtected(false);
  }

  if (!RTC.GetIsRunning())
  {
    Serial.println("DS1302 RTC was not actively running, starting now");
    RTC.SetIsRunning(true);
  }
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: RTC DS1302 initialized!");
#endif
}

uint32_t RtcGet()
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: Calling DS1302 RTC.GetDateTime()...");
#endif
  RtcDateTime temptime;
  temptime = RTC.GetDateTime();
  uint32_t returnvalue = temptime.Unix32Time();
#ifdef DEBUG_OUTPUT_RTC
  Serial.print("DEBUG_OUTPUT_RTC: DS1302 RTC.GetDateTime() returned: ");
  Serial.println(returnvalue);
#endif
  return returnvalue;
}

void RtcSet(uint32_t tt)
{
#ifdef DEBUG_OUTPUT_RTC
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
#elif defined(HARDWARE_NOVELLIFE_CLOCK) || defined(HARDWARE_MARVELTUBES_CLOCK) // R8025T RTC chip
#include <RTC_RX8025T.h>

RX8025T RTC;

void RtcBegin()
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("");
  Serial.println("DEBUG_OUTPUT_RTC: Trying to call RX8025T RTC.Init()");
#endif
#if defined(HARDWARE_NOVELLIFE_CLOCK)
  RTC.init(RTC_SDA_PIN, RTC_SCL_PIN, Wire1); // NovelLife uses the secondary I2C bus
#else
  RTC.init(RTC_SDA_PIN, RTC_SCL_PIN, Wire); // MarvelTubes uses the default I2C bus
#endif
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

  // int ret = RTC_RX8025T.set(tt);
  int ret = RTC.set(tt); // set the RTC time
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
#else // For EleksTube and all other clocks with DS3231 RTC chip or DS1307/PCF8523.
#include <RTClib.h>

RTC_DS3231 RTC; // DS3231, works also with DS1307 or PCF8523

void RtcBegin()
{
  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
  if (!RTC.begin())
  {
    Serial.println("No supported RTC found!");
  }
#ifdef DEBUG_OUTPUT_RTC
  else
  {
    bool RegReadSuccess = false;
    unsigned int ctrl = 0;

    Serial.println("DEBUG_OUTPUT_RTC: DS3231/DS1307 RTC found!");
    Serial.printf("Square Wave output status: 0x%x\r\n", RTC.readSqwPinMode());
    Serial.printf("32KHz output status: %s\r\n", RTC.isEnabled32K() ? "Enabled" : "Disabled");
    // Manually read the control and status registers of the DS3231.
    Wire.beginTransmission(0x68);
    Wire.write(0x0E); // Address of the control register
    Wire.endTransmission();
    RegReadSuccess = Wire.requestFrom(0x68, 2);
    Serial.printf("Read from control and status registers: %s\r\n", RegReadSuccess ? "Success" : "Failed!");
    if (RegReadSuccess)
    {
      ctrl = Wire.read();
      Serial.println("DS3231 Control Register:");
      Serial.printf("EOSC:%d BBSQW:%d CONV:%d RS2:%d RS1:%d INTCN:%d A2IE:%d A1IE:%d\r\n",
                    (ctrl & 0x80) >> 7, (ctrl & 0x40) >> 6, (ctrl & 0x20) >> 5, (ctrl & 0x10) >> 4, (ctrl & 0x08) >> 3,
                    (ctrl & 0x04) >> 2, (ctrl & 0x02) >> 1, (ctrl & 0x01));

      ctrl = ctrl >> 8;
      Serial.println("DS3231 Status Register:");
      Serial.printf("OSF:%d EN32Khz:%d BSY:%d A2F:%d A1F:%d\r\n",
                    (ctrl & 0x80) >> 7, (ctrl & 0x08) >> 3, (ctrl & 0x04) >> 2,
                    (ctrl & 0x02) >> 1, (ctrl & 0x01));
    }
    Serial.println("Forcing temperature conversion now.");
    Wire.beginTransmission(0x68);
    Wire.write(0x0E);
    Wire.write(0x3C); // Set CONV=1, set RS2,RS1,INTCN = 1
    Wire.endTransmission();
    delay(5); // Allow some time for BSY to be set

    Wire.beginTransmission(0x68);
    Wire.write(0x0F);
    Wire.endTransmission();
    RegReadSuccess = Wire.requestFrom(0x68, 1);
    if (RegReadSuccess)
    {
      ctrl = 0;
      ctrl = Wire.read();
      Serial.printf("Temperature conversion busy flag: %s\r\n", ((ctrl & 0x04) >> 2) ? "Set!" : "Not set..");
      Serial.println("Waiting 2 seconds for temperature conversion to finish.");
      delay(2000);
      Serial.printf("DS3231 Temperature: %f C\r\n", RTC.getTemperature());
    }
    else
    {
      Serial.println("Unable to read from DS3231!");
    }
  }
#endif

  // Check if the RTC chip reports a power failure.
  bool bPowerLost = 0;
  bPowerLost = RTC.lostPower();
  if (bPowerLost)
  {
    Serial.println("DS3231/DS1307 RTC reports power was lost! Setting time to default value.");
    RTC.adjust(DateTime(2023, 1, 1, 0, 0, 0)); // Set the RTC time to a default value
  }
  else
  {
#ifdef DEBUG_OUTPUT_RTC
    Serial.println("DEBUG_OUTPUT_RTC: DS3231/DS1307 RTC power is OK!");
#endif
  }
}

uint32_t RtcGet()
{
  DateTime now = RTC.now(); // Convert to Unix time
  uint32_t returnvalue = now.unixtime();
#ifdef DEBUG_OUTPUT_RTC
  Serial.print("DEBUG_OUTPUT_RTC: DS3231/DS1307 RTC now.unixtime() returned: ");
  Serial.println(returnvalue);
#endif
  return returnvalue;
}

void RtcSet(uint32_t tt)
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.print("DEBUG_OUTPUT_RTC: Attempting to set DS3231/DS1307 RTC to: ");
  Serial.println(tt);
#endif

  DateTime timetoset(tt); // Convert to Unix time
  RTC.adjust(timetoset);  // Set the RTC time
#ifdef DEBUG_OUTPUT
  Serial.println("DEBUG_OUTPUT_RTC: DS3231/DS1307 RTC time updated.");
#endif
}
#endif // End of RTC chip selection

//-----------------------------------------------------------------------------------------------
// end of RTC chip stuff
//-----------------------------------------------------------------------------------------------

//-----------------------------------------------------------------------------------------------
// clock class stuff
//-----------------------------------------------------------------------------------------------

// Global variables for clock/NTP client
uint32_t Clock::millis_last_ntp = 0;
WiFiUDP Clock::ntpUDP;
NTPClient Clock::ntpTimeClient(ntpUDP, NTP_SERVER, 0, NTP_UPDATE_INTERVAL);

// Adaptive NTP sync variables
uint32_t Clock::current_ntp_interval_ms = Clock::ntp_interval_initial_ms;
uint8_t Clock::consecutive_failures = 0;
uint8_t Clock::consecutive_successes = 0;

void Clock::begin(StoredConfig::Config::Clock *config_)
{
  config = config_;

  if (config->is_valid != StoredConfig::valid)
  {
    // Config is invalid, probably a new device never had its config written.
    // Load some reasonable defaults.
    Serial.println("Loaded Clock config is invalid, using default config values. This is normal on first boot.");
    setTwelveHour(false);
    setBlankHoursZero(false);
    setTimeZoneOffset(1 * 3600); // CET
    setActiveGraphicIdx(1);
    config->is_valid = StoredConfig::valid;
  }

  RtcBegin();            // Initialize the RTC chip
  ntpTimeClient.begin(); // Initialize the NTP client

  // Don't update the NTP time immediately here!!! Wait for the first loop() call!
  // Or the update interval will be too short and the initial call in the loop() will fail.

  // Set the sync provider for TimeLib to our syncProvider method
  setSyncProvider(&Clock::syncProvider);

  // Set TimeLib sync interval to current adaptive interval
  setSyncInterval(current_ntp_interval_ms / 1000); // This will make TimeLib call syncProvider() adaptively.

#ifdef DEBUG_NTPClient
  Serial.print("DEBUG_NTPClient: Initial NTP sync interval set to ");
  Serial.print(current_ntp_interval_ms / 1000);
  Serial.print("s (");
  Serial.print(current_ntp_interval_ms / 60000);
  Serial.println(" min)");
#endif
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
  Serial.println("DEBUG_OUTPUT_RTC: Clock:syncProvider() entered.");
#endif
  time_t rtc_now;

#ifdef DEBUG_NTPClient
  uint32_t current_millis = millis();
  Serial.print("\nDEBUG_NTPClient: Clock:syncProvider() - milis_last_ntp: ");
  Serial.println(millis_last_ntp);
  Serial.print("DEBUG_NTPClient: Clock:syncProvider() - current_ntp_interval_ms: ");
  Serial.println(current_ntp_interval_ms);
  Serial.print("DEBUG_NTPClient: Clock:syncProvider() - millis(): ");
  Serial.println(current_millis);
  Serial.print("DEBUG_NTPClient: Clock:syncProvider() - millis() - millis_last_ntp ");
  Serial.println(current_millis - millis_last_ntp);
  Serial.print("DEBUG_NTPClient: Clock:syncProvider() - millis_last_ntp == 0: ");
  Serial.println(millis_last_ntp == 0);
#endif

  // check if we need to update from the NTP time
  if (millis() - millis_last_ntp >= current_ntp_interval_ms || millis_last_ntp == 0) // Adaptive interval timing
  {                                                                                  // It's time to get a new NTP sync
    Serial.println("\nTime to update from NTP Server...");
    if (WifiState == connected)
    { // We have WiFi, so try to get NTP time.
      if (ntpTimeClient.update())
      {
        Serial.println("NTP update query was successful!");
        time_t ntp_now = ntpTimeClient.getEpochTime();
        Serial.print("NTP time = ");
        Serial.println(ntpTimeClient.getFormattedTime());
        rtc_now = RtcGet(); // Get the RTC time again, because it may have changed in the meantime
        // Sync the RTC to NTP if needed.
        Serial.print("NTP: ");
        Serial.println(ntp_now);
        Serial.print("RTC: ");
        Serial.println(rtc_now);
        Serial.print("Diff: ");
        Serial.println(ntp_now - rtc_now);

        if ((ntp_now != rtc_now) && (ntp_now > 1761609600)) // check if we have a difference and a valid NTP time (check for after 1761609600 = 2025-10-28 00:00:01 UTC)
        {                                                   // NTP time is valid and different from RTC time
          Serial.println("RTC and NTP time differs more than 1 second, updating RTC time.");
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
        millis_last_ntp = millis(); // Store the last time we tried to get NTP time
        handleNtpSuccess();         // Update adaptive timing

        Serial.println("Using NTP time!");
        return ntp_now;
      }
      else
      { // NTP return value is not valid
        Serial.println("NTP update query was not successful!\nUsing RTC time!");
        millis_last_ntp = millis(); // store the last attempt time even on failure
        handleNtpFailure();         // Update adaptive timing
        rtc_now = RtcGet();         // Get the RTC time
        return rtc_now;
      }
    } // no WiFi!
    Serial.println("No WiFi!\nUsing RTC time!");
    millis_last_ntp = millis(); // store the last attempt time even on WiFi failure
    handleNtpFailure();         // Update adaptive timing for WiFi failures too
    rtc_now = RtcGet();         // Get the RTC time (if update interval not reached or no WiFi or NTP failure)
    return rtc_now;
  }
  Serial.println("Using RTC time.");
  rtc_now = RtcGet(); // read RTC time if no NTP update is needed
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

// Adaptive NTP sync methods
void Clock::handleNtpSuccess()
{
  consecutive_failures = 0;
  consecutive_successes++;

#ifdef DEBUG_NTPClient
  Serial.print("DEBUG_NTPClient: NTP Success #");
  Serial.println(consecutive_successes);
#endif

  updateNtpInterval();
}

void Clock::handleNtpFailure()
{
  consecutive_successes = 0;
  consecutive_failures++;

#ifdef DEBUG_NTPClient
  Serial.print("DEBUG_NTPClient: NTP Failure #");
  Serial.println(consecutive_failures);
#endif

  updateNtpInterval();
}

void Clock::updateNtpInterval()
{
  uint32_t old_interval = current_ntp_interval_ms;

  if (consecutive_failures > 0)
  { // After failures: use error interval, increase with more failures
    if (consecutive_failures == 1)
      current_ntp_interval_ms = ntp_interval_error_ms; // 10 min
    else if (consecutive_failures >= 3)
      current_ntp_interval_ms = min(ntp_interval_error_ms * (1 << (consecutive_failures - 1)), ntp_interval_max_ms); // After 3+ failures: double the interval up to maximum
  }
  else if (consecutive_successes >= 6)
    current_ntp_interval_ms = ntp_interval_stable_ms; // After 6+ successes: move to stable interval (1 hour)
  else if (consecutive_successes >= 3)
    current_ntp_interval_ms = ntp_interval_normal_ms; // After 3+ successes: move to normal interval (30 min)
  else
    current_ntp_interval_ms = ntp_interval_initial_ms; // Initial or few successes: keep initial interval (5 min)

  // Update TimeLib sync interval if it changed
  if (old_interval != current_ntp_interval_ms)
    setSyncInterval(current_ntp_interval_ms / 1000);

#ifdef DEBUG_NTPClient
  if (old_interval != current_ntp_interval_ms)
  {
    Serial.print("DEBUG_NTPClient: NTP interval changed from ");
    Serial.print(old_interval / 1000);
    Serial.print("s to ");
    Serial.print(current_ntp_interval_ms / 1000);
    Serial.print("s (");
    Serial.print(current_ntp_interval_ms / 60000);
    Serial.println(" min)");
  }
#endif
}
