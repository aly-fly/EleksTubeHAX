#include "Clock.h"
#include "WiFi_WPS.h"

#if defined(HARDWARE_SI_HAI_CLOCK) || defined(HARDWARE_IPSTUBE_CLOCK) // for Clocks with DS1302 chip (SI HAI or IPSTUBE)
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
#elif defined(HARDWARE_NovelLife_SE_CLOCK) // for NovelLife_SE clone with R8025T RTC chip
#include <RTC_RX8025T.h>                   // This header will now use Wire1 for I2C operations.

RX8025T RTC;

void RtcBegin()
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("");
  Serial.println("DEBUG_OUTPUT_RTC: Trying to call RX8025T RTC.Init()");
#endif
  //RTC_RX8025T.init((uint32_t)RTC_SDA_PIN, (uint32_t)RTC_SCL_PIN, Wire1); // setup second I2C for the RX8025T RTC chip
  RTC.init(RTC_SDA_PIN, RTC_SCL_PIN, Wire1); // setup second I2C for the RX8025T RTC chip
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

  //int ret = RTC_RX8025T.set(tt);
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
  //uint32_t returnvalue = RTC_RX8025T.get(); // Get the RTC time
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
    Serial.println("No supported RTC found!");
  }
  #ifdef DEBUG_OUTPUT_RTC
  else
  {
    bool RegReadSuccess = false;
    unsigned int ctrl = 0;

    Serial.println("DEBUG_OUTPUT_RTC: DS3231/DS1307 RTC found!");
    Serial.printf("Square Wave output status: 0x%x\r\n", RTC.readSqwPinMode());
    Serial.printf("32KHz output status: %s\r\n", RTC.isEnabled32K() ? "Enabled":"Disabled");
    //manually read the control and status registers of the DS3231
    Wire.beginTransmission(0x68);
    Wire.write(0x0E); //address of the control register, 0x0E
    Wire.endTransmission();
    RegReadSuccess = Wire.requestFrom(0x68, 2);
    Serial.printf("Read from control and status registers: %s\r\n",  RegReadSuccess ? "Success":"Failed!");
    if (RegReadSuccess)
    {
      ctrl = Wire.read();
      Serial.println("DS3231 Control Register:");
      Serial.printf("EOSC:%d BBSQW:%d CONV:%d RS2:%d RS1:%d INTCN:%d A2IE:%d A1IE:%d\r\n", 
        (ctrl & 0x80) >> 7, (ctrl & 0x40) >> 6, (ctrl & 0x20) >> 5, (ctrl & 0x10) >> 4, (ctrl & 0x08) >> 3,
        (ctrl & 0x04) >> 2, (ctrl & 0x02)>> 1, (ctrl & 0x01) );

      ctrl = ctrl >> 8;
      Serial.println("DS3231 Status Register:");
      Serial.printf("OSF:%d EN32Khz:%d BSY:%d A2F:%d A1F:%d\r\n",
        (ctrl & 0x80) >> 7, (ctrl & 0x08) >> 3, (ctrl & 0x04) >> 2, 
        (ctrl & 0x02)>> 1, (ctrl & 0x01) );
    }
    Serial.println("Forcing temperature conversion now.");
    Wire.beginTransmission(0x68);
    Wire.write(0x0E);
    Wire.write(0x3C); //Set CONV=1, set RS2,RS1,INTCN = 1
    Wire.endTransmission();
    delay(5);//allow some time for BSY to be set
    
    Wire.beginTransmission(0x68);
    Wire.write(0x0F);
    Wire.endTransmission();
    RegReadSuccess = Wire.requestFrom(0x68, 1);
    if (RegReadSuccess)
    {
      ctrl = 0;
      ctrl = Wire.read();
      Serial.printf("Temperature conversion busy flag: %s\r\n", ((ctrl & 0x04) >> 2) ? "Set!":"Not set..");
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


  // check if the RTC chip reports a power failure
  bool bPowerLost = 0;
  bPowerLost = RTC.lostPower();
  if (bPowerLost)
  {
    Serial.println("DS3231/DS1307 RTC reports power was lost! Setting time to default value.");
    RTC.adjust(DateTime(2023, 1, 1, 0, 0, 0)); // set the RTC time to a default value
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
  DateTime now = RTC.now(); // convert to unix time
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

  DateTime timetoset(tt); // convert to unix time
  RTC.adjust(timetoset);  // set the RTC time
#ifdef DEBUG_OUTPUT
  Serial.println("DEBUG_OUTPUT_RTC: DS3231/DS1307 RTC time updated.");
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
    Serial.println("Loaded Clock config is invalid, using default config values. This is normal on first boot.");
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
  //millis_last_ntp = millis();
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
  Serial.println("DEBUG_OUTPUT_RTC: Clock:syncProvider() entered.");
#endif
  time_t rtc_now;
  rtc_now = RtcGet(); // Get the RTC time

  if (millis() - millis_last_ntp > refresh_ntp_every_ms || millis_last_ntp == 0) // Get NTP time only every 10 minutes or if not yet done
  { // It's time to get a new NTP sync
    if (WifiState == connected)
    { // We have WiFi, so try to get NTP time.
      Serial.print("Try to get the actual time from NTP server...");
      if (ntpTimeClient.update())
      {
        Serial.println("NTP query done.");
        time_t ntp_now = ntpTimeClient.getEpochTime();
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
