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

#define _BV2(bit) (1 << (bit))

// RX8025T I2C Address
#define RX8025T_ADDR 0x32

// RX8025T Register Addresses
#define RX8025T_SECONDS 0x00
#define RX8025T_MINUTES 0x01
#define RX8025T_HOURS 0x02
#define RX8025T_DAY 0x03
#define RX8025T_DATE 0x04
#define RX8025T_MONTH 0x05
#define RX8025T_YEAR 0x06
#define RX8025T_RAM 0x07
#define RX8025T_ALARM_MINUTES 0x08
#define RX8025T_ALARM_HOURS 0x09
#define RX8025T_ALARM_DAYDATE 0x0A
#define RX8025T_TIMER_COUNTER_0 0x0B
#define RX8025T_TIMER_COUNTER_1 0x0C
#define RX8025T_RTC_EXT 0x0D
#define RX8025T_RTC_STATUS 0x0E
#define RX8025T_RTC_CONTROL 0x0F

// Extension register bits
#define TSEL0 0
#define TSEL1 1
#define FSEL0 2
#define FSEL1 3
#define TE 4
#define USEL 5
#define WADA 6

// Status register bits
#define VDET 0
#define VLF 1
#define AF 3
#define TF 4
#define UF 5

// Control register bits
#define RESET 0
#define AIE 3
#define TIE 4
#define UIE 5
#define CSEL0 6
#define CSEL1 7

// Time update interrupt function
#define INT_SECOND 0x00
#define INT_MINUTE 0x20

// Time update interrupt
#define INT_ON 0x20
#define INT_OFF 0x00

// Temperature compensation interval
#define INT_0_5_SEC 0x00
#define INT_2_SEC 0x40
#define INT_10_SEC 0x80
#define INT_30_SEC 0xC0

// FOUT frequency
#define FOUT_32768 0x00 // or 0x0C
#define FOUT_1024 0x04
#define FOUT_1 0x08

RX8025T::RX8025T() : i2cBus(&Wire) // Initialize i2cBus with default Wire instance
{
}

/*----------------------------------------------------------------------*
 * I2C start, RTC initialization, cleaning of registers and flags.      *
 * If the VLF flag is "1" there was data loss or                        *
 * supply voltage drop or powering up from 0V.                          *
 * If VDET flag is "1" temperature compensation is not working.         *
 * Default settings.                                                    *
 *----------------------------------------------------------------------*/
void RX8025T::init(uint32_t rtcSDA, uint32_t rtcSCL, TwoWire &wireBus)
{
  i2cBus = &wireBus; // Assign the passed TwoWire instance to the member variable

#ifdef DEBUG_OUTPUT_RTC
  Serial.println("DEBUG_OUTPUT_RTC: RX8025T RTC SDA pin: " + String(rtcSDA) + " and SCL pin: " + String(rtcSCL));
#endif

  if (rtcSDA != -1 && rtcSCL != -1)
  {
    i2cBus->begin(rtcSDA, rtcSCL); // Custom initialization with specified SDA and SCL pins
  }
  else
  {
    i2cBus->begin(); // Default initialization with default SDA and SCL pins
  }

  uint8_t statusReg = readRTC(RX8025T_RTC_STATUS);
  uint8_t mask = _BV(VLF) | _BV(VDET);

  if (statusReg & mask)
  {
#ifdef DEBUG_OUTPUT_RTC
    Serial.println("DEBUG_OUTPUT_RTC: Resetting RX8025T due to VLF or VDET flag.");
#endif
    writeRTC(RX8025T_RTC_CONTROL, _BV(RESET)); // Reset module
  }

  // Clear control registers
  writeRTC(RX8025T_RTC_EXT, 0x00);
  writeRTC(RX8025T_RTC_STATUS, 0x00);
  writeRTC(RX8025T_RTC_CONTROL, (0x00 | INT_2_SEC));
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
  {
#ifdef DEBUG_OUTPUT_RTC
    Serial.println("DEBUG_OUTPUT_RTC: Failed to read time from RX8025T.");
#endif
    return 0;
  }

  return makeTime(tm);
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

  return write(tm);
}

/*----------------------------------------------------------------------*
 * Reads the current time from the RTC and returns it in a tmElements_t *
 * structure. Returns the I2C status (zero if successful).              *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::read(tmElements_t &tm)
{
  i2cBus->beginTransmission(RX8025T_ADDR);
  i2cBus->write((uint8_t)RX8025T_SECONDS);
  if (uint8_t e = i2cBus->endTransmission())
  {
#ifdef DEBUG_OUTPUT_RTC
    Serial.print("DEBUG_OUTPUT_RTC: I2C error during read: ");
    Serial.println(e);
#endif
    return e;
  }

  i2cBus->requestFrom(RX8025T_ADDR, tmNbrFields);
  tm.Second = bcd2dec(i2cBus->read());
  tm.Minute = bcd2dec(i2cBus->read());
  tm.Hour = bcd2dec(i2cBus->read());
  tm.Wday = bin2wday(i2cBus->read());
  tm.Day = bcd2dec(i2cBus->read());
  tm.Month = bcd2dec(i2cBus->read());
  tm.Year = y2kYearToTm(bcd2dec(i2cBus->read()));

  return 0;
}

/*----------------------------------------------------------------------*
 * Sets the RTC's time from a tmElements_t structure and clears the     *
 * oscillator stop flag (OSF) in the Control/Status register.           *
 * Returns the I2C status (zero if successful).                         *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::write(tmElements_t &tm)
{
  i2cBus->beginTransmission(RX8025T_ADDR);
  i2cBus->write((uint8_t)RX8025T_SECONDS);
  i2cBus->write(dec2bcd(tm.Second));
  i2cBus->write(dec2bcd(tm.Minute));
  i2cBus->write(dec2bcd(tm.Hour));
  i2cBus->write(wday2bin(tm.Wday));
  i2cBus->write(dec2bcd(tm.Day));
  i2cBus->write(dec2bcd(tm.Month));
  i2cBus->write(dec2bcd(tmYearToY2k(tm.Year)));
  return i2cBus->endTransmission();
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
  i2cBus->beginTransmission(RX8025T_ADDR);
  i2cBus->write(addr);
  for (uint8_t i = 0; i < nBytes; i++)
  {
    i2cBus->write(values[i]);
  }
  return i2cBus->endTransmission();
}

/*----------------------------------------------------------------------*
 * Write a single uint8_t to RTC RAM.                                   *
 * Valid address range is 0x00 - 0xFF, no checking.                     *
 * Returns the I2C status (zero if successful).                         *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::writeRTC(uint8_t addr, uint8_t value)
{
  return writeRTC(addr, &value, 1);
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
  i2cBus->beginTransmission(RX8025T_ADDR);
  i2cBus->write(addr);
  if (uint8_t e = i2cBus->endTransmission())
  {
#ifdef DEBUG_OUTPUT_RTC
    Serial.print("DEBUG_OUTPUT_RTC: I2C error during read: ");
    Serial.println(e);
#endif
    return e;
  }

  i2cBus->requestFrom((uint8_t)RX8025T_ADDR, nBytes);
  for (uint8_t i = 0; i < nBytes; i++)
  {
    values[i] = i2cBus->read();
  }

  return 0;
}

/*----------------------------------------------------------------------*
 * Read a single uint8_t from RTC RAM.                                  *
 * Valid address range is 0x00 - 0xFF, no checking.                     *
 *----------------------------------------------------------------------*/
uint8_t RX8025T::readRTC(uint8_t addr)
{
  uint8_t value;
  readRTC(addr, &value, 1);

  return value;
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
