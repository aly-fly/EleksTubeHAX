/*-----------------------------------------------------------------------*
 * RTC_RX8025T.cpp - Arduino library for the Seiko Epson RX8025T         *
 * Real Time Clock. Modified version of the original library.            *
 *                                                                       *
 * This library has been modified for custom usage in the EleksTubeHAX   *
 * project.                                                              *
 * Original library from Marcin Saj                                      *
 * Original library based on the Paul Stoffregen DS3232RTC.              *
 *                                                                       *
 * The MIT License                                                       *
 * Marcin Saj 25 OCT 2022                                                *
 * Modified by Clemens Sutor (Martinius79) on 07-04-2025                 *
 * https://github.com/marcinsaj/RTC_RX8025T                              *
 *-----------------------------------------------------------------------*/

#include <RTC_RX8025T.h>
#include <Wire.h>

RX8025T::RX8025T()
{
}
/*----------------------------------------------------------------------*
 * I2C start, RTC initialization, cleaning of registers and flags.      *
 * If the VLF flag is "1" there was data loss or                        *
 * supply voltage drop or powering up from 0V.                          *
 * If VDET flag is "1" temperature compensation is not working.         *
 * Default settings.                                                    *
 *----------------------------------------------------------------------*/
void RX8025T::init(void)
{
  uint8_t statusReg, mask;

  Wire1.begin();

  statusReg = readRTC(RX8025T_RTC_STATUS);
  mask = _BV(VLF) | _BV(VDET);

  if (statusReg & mask)
  {
    writeRTC(RX8025T_RTC_CONTROL, _BV(RESET)); // Reset module
  }

  // Clear control registers
  writeRTC(RX8025T_RTC_EXT, 0x00);
  writeRTC(RX8025T_RTC_STATUS, 0x00);
  // Dafault value of temperature compensation interval is 2 seconds
  writeRTC(RX8025T_RTC_CONTROL, (0x00 | INT_2_SEC));
}

void RX8025T::init(uint32_t rtcSDA, uint32_t rtcSCL)
{
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: Entering RX8025T_init(uint32_t, uint32_t)");
  Serial.print("DEBUG_OUTPUT_RTC: Initializing Wire1 with SDA = ");
  Serial.print(rtcSDA);
  Serial.print(", SCL = ");
  Serial.println(rtcSCL);
#endif
  Wire1.begin(rtcSDA, rtcSCL);
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: Wire1.begin completed");
#endif

  uint8_t statusReg, mask;
  statusReg = readRTC(RX8025T_RTC_STATUS);
#ifdef DEBUG_OUTPUT_RTC
  Serial.print("DEBUG_OUTPUT_RTC: Read status register: 0x");
  Serial.println(statusReg, HEX);
#endif
  mask = _BV(VLF) | _BV(VDET);

  if (statusReg & mask)
  {
#ifdef DEBUG_OUTPUT_RTC
    Serial.println("DEBUG_OUTPUT_RTC: Status flags indicate data loss or temp compensation error, resetting RTC module.");
#endif
    writeRTC(RX8025T_RTC_CONTROL, _BV(RESET)); // Reset module
  }

  // Clear control registers
  writeRTC(RX8025T_RTC_EXT, 0x00);
  writeRTC(RX8025T_RTC_STATUS, 0x00);
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: Control registers cleared");
#endif
  // Dafault value of temperature compensation interval is 2 seconds
  writeRTC(RX8025T_RTC_CONTROL, (0x00 | INT_2_SEC));
#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: Temperature compensation interval set to 2 seconds; RTC init complete.");
#endif
}

/*----------------------------------------------------------------------*
 * Reads the current time from the RTC and returns it as a time_t       *
 * value. Returns a zero value if an I2C error occurred (e.g. RTC       *
 * not present).                                                        *
 *----------------------------------------------------------------------*/
time_t RX8025T::get()
{
  tmElements_t tm;

  if (read(tm))
    return 0;
  return (makeTime(tm));
}

/*----------------------------------------------------------------------*
 * Sets the RTC to the given time_t value and clears the                *
 * oscillator stop flag (OSF) in the Control/Status register.           *
 * Returns the I2C status (zero if successful).                         *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::set(time_t t)
{
  tmElements_t tm;

  breakTime(t, tm);
  return (write(tm));
}

/*----------------------------------------------------------------------*
 * Reads the current time from the RTC and returns it in a tmElements_t *
 * structure. Returns the I2C status (zero if successful).              *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::read(tmElements_t &tm)
{
  Wire1.beginTransmission(RX8025T_ADDR);
  Wire1.write((uint8_t)RX8025T_SECONDS);
  if (uint8_t e = Wire1.endTransmission())
    return e;
  // request 7 bytes (secs, min, hr, dow, date, mth, yr)
  Wire1.requestFrom(RX8025T_ADDR, tmNbrFields);
  tm.Second = bcd2dec(Wire1.read());
  tm.Minute = bcd2dec(Wire1.read());
  tm.Hour = bcd2dec(Wire1.read());
  tm.Wday = bin2wday(Wire1.read());
  tm.Day = bcd2dec(Wire1.read());
  tm.Month = bcd2dec(Wire1.read());
  tm.Year = y2kYearToTm(bcd2dec(Wire1.read()));
  return 0;
}

/*----------------------------------------------------------------------*
 * Sets the RTC's time from a tmElements_t structure and clears the     *
 * oscillator stop flag (OSF) in the Control/Status register.           *
 * Returns the I2C status (zero if successful).                         *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::write(tmElements_t &tm)
{
  Wire1.beginTransmission(RX8025T_ADDR);
  Wire1.write((uint8_t)RX8025T_SECONDS);
  Wire1.write(dec2bcd(tm.Second));
  Wire1.write(dec2bcd(tm.Minute));
  Wire1.write(dec2bcd(tm.Hour));
  Wire1.write(wday2bin(tm.Wday));
  Wire1.write(dec2bcd(tm.Day));
  Wire1.write(dec2bcd(tm.Month));
  Wire1.write(dec2bcd(tmYearToY2k(tm.Year)));
  uint8_t ret = Wire1.endTransmission();
  return ret;
}

/*----------------------------------------------------------------------*
 * Write multiple bytes to RTC RAM.                                     *
 * Valid address range is 0x00 - 0xFF, no checking.                     *
 * Number of bytes (nBytes) must be between 1 and 31 (Wire library      *
 * limitation).                                                         *
 * Returns the I2C status (zero if successful).                         *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::writeRTC(uint8_t addr, uint8_t *values, uint8_t nBytes)
{
  Wire1.beginTransmission(RX8025T_ADDR);
  Wire1.write(addr);
  for (uint8_t i = 0; i < nBytes; i++)
    Wire1.write(values[i]);
  return Wire1.endTransmission();
}

/*----------------------------------------------------------------------*
 * Write a single uint8_t to RTC RAM.                                   *
 * Valid address range is 0x00 - 0xFF, no checking.                     *
 * Returns the I2C status (zero if successful).                         *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::writeRTC(uint8_t addr, uint8_t value)
{
  return (writeRTC(addr, &value, 1));
}

/*----------------------------------------------------------------------*
 * Read multiple bytes from RTC RAM.                                    *
 * Valid address range is 0x00 - 0xFF, no checking.                     *
 * Number of bytes (nBytes) must be between 1 and 32 (Wire library      *
 * limitation).                                                         *
 * Returns the I2C status (zero if successful).                         *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::readRTC(uint8_t addr, uint8_t *values, uint8_t nBytes)
{
  Wire1.beginTransmission(RX8025T_ADDR);
  Wire1.write(addr);
  if (uint8_t e = Wire1.endTransmission())
    return e;
  Wire1.requestFrom((uint8_t)RX8025T_ADDR, nBytes);
  for (uint8_t i = 0; i < nBytes; i++)
    values[i] = Wire1.read();
  return 0;
}

/*----------------------------------------------------------------------*
 * Read a single uint8_t from RTC RAM.                                  *
 * Valid address range is 0x00 - 0xFF, no checking.                     *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::readRTC(uint8_t addr)
{
  uint8_t b;

  readRTC(addr, &b, 1);
  return b;
}

/*----------------------------------------------------------------------*
 * Time update interrupt initialization setup.                          *
 * Function generates - INT output - interrupt events at one-second     *
 * or one-minute intervals.                                             *
 * USEL bit "0"-every second, "1"-every minute.                         *
 *----------------------------------------------------------------------*/
void RX8025T::initTUI(uint8_t option)
{
  uint8_t extReg, mask;

  extReg = readRTC(RX8025T_RTC_EXT);
  mask = _BV(USEL);

  extReg = (extReg & ~mask) | (mask & option);
  writeRTC(RX8025T_RTC_EXT, extReg);
}

/*----------------------------------------------------------------------*
 * Time update interrupt event ON/OFF.                                  *
 * UIE bit "1"-ON, "0"-OFF                                              *
 *----------------------------------------------------------------------*/
void RX8025T::statusTUI(uint8_t status)
{
  uint8_t controlReg, mask;

  controlReg = readRTC(RX8025T_RTC_CONTROL);
  mask = _BV(UIE);

  controlReg = (controlReg & ~mask) | (mask & status);
  writeRTC(RX8025T_RTC_CONTROL, controlReg);
}

/*----------------------------------------------------------------------*
 * Time update interrupt UF status flag.                                *
 * If for some reason the hardware interrupt has not been               *
 * registered/handled, it is always possible to check the UF flag.      *
 * If UF"1" the interrupt has occurred.                                 *
 *----------------------------------------------------------------------*/
bool RX8025T::checkTUI(void)
{
  uint8_t statusReg, mask;

  statusReg = readRTC(RX8025T_RTC_STATUS);
  mask = _BV(UF);

  if (statusReg & mask)
  {
    // Clear UF flag
    statusReg = statusReg & ~mask;
    writeRTC(RX8025T_RTC_STATUS, statusReg);
    return 1;
  }
  else
    return 0;
}

/*----------------------------------------------------------------------*
 * Temperature compensation interval settings.                          *
 * CSEL0, CSEL1 - 0.5s, 2s-default, 10s, 30s                            *
 *----------------------------------------------------------------------*/
void RX8025T::tempCompensation(uint8_t option)
{
  uint8_t controlReg, mask;

  controlReg = readRTC(RX8025T_RTC_CONTROL);
  mask = _BV(CSEL0) | _BV(CSEL1);

  controlReg = (controlReg & ~mask) | (mask & option);
  writeRTC(RX8025T_RTC_CONTROL, controlReg);
}

/*----------------------------------------------------------------------*
 * FOUT frequency output settings                                       *
 * If FOE input = "H" (high level) then FOUT is active.                 *
 * FSEL0, FSEL1 - 32768Hz, 1024H, 1Hz                                   *
 *----------------------------------------------------------------------*/
void RX8025T::initFOUT(uint8_t option)
{

  uint8_t extReg, mask;

  extReg = readRTC(RX8025T_RTC_EXT);
  mask = _BV(FSEL0) | _BV(FSEL1);

  extReg = (extReg & ~mask) | (mask & option);
  writeRTC(RX8025T_RTC_EXT, extReg);
}

/*----------------------------------------------------------------------*
 * Decimal-to-Dedicated format conversion - RTC datasheet page 12       *
 * Sunday 0x01,...Wednesday 0x08,...Saturday 0x40                       *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::wday2bin(uint8_t wday)
{
  return _BV(wday - 1);
}

/*----------------------------------------------------------------------*
 * Dedicated-to-Decimal format conversion - RTC datasheet page 12       *
 * Sunday 1, Monday 2, Tuesday 3...                                     *
 *----------------------------------------------------------------------*/
uint8_t __attribute__((noinline)) RX8025T::bin2wday(uint8_t wday)
{
  for (int i = 0; i < 7; i++)
  {
    if ((wday >> i) == 1)
      wday = i + 1;
  }

  return wday;
}

/*----------------------------------------------------------------------*
 * Decimal-to-BCD conversion                                            *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::dec2bcd(uint8_t n)
{
  return n + 6 * (n / 10);
}

/*----------------------------------------------------------------------*
 * BCD-to-Decimal conversion                                            *
 *----------------------------------------------------------------------*/
uint8_t __attribute__((noinline)) RX8025T::bcd2dec(uint8_t n)
{
  return n - 6 * (n >> 4);
}

RX8025T RTC_RX8025T = RX8025T();
