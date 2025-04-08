/*-----------------------------------------------------------------------*
 * MODIFIED RTC_RX8025T.h - Arduino library for the Seiko Epson RX8025T  *
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

#ifndef RTC_RX8025T_h
#define RTC_RX8025T_h

#include <TimeLib.h>
#include "../../src/_USER_DEFINES.h" // User defines (located in the src folder)

#if defined(ARDUINO) && ARDUINO >= 100
#include <Arduino.h>
#else
#include <WProgram.h>
#endif

// #define _BV2(bit) (1 << (bit))

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

class RX8025T
{
public:
        RX8025T();
        void init(void);
        void init(uint32_t SDA, uint32_t SCL);
        static time_t get(void); // Must be static to work with setSyncProvider() in the Time library
        uint8_t set(time_t t);
        static uint8_t read(tmElements_t &tm);
        uint8_t write(tmElements_t &tm);
        uint8_t writeRTC(uint8_t addr, uint8_t *values, uint8_t nBytes);
        uint8_t writeRTC(uint8_t addr, uint8_t value);
        uint8_t readRTC(uint8_t addr, uint8_t *values, uint8_t nBytes);
        uint8_t readRTC(uint8_t addr);
        void tempCompensation(uint8_t option);
        void initFOUT(uint8_t option);
        void initTUI(uint8_t option);
        void statusTUI(uint8_t status);
        bool checkTUI(void);

private:
        uint8_t currentStateUIEbit;
        uint8_t wday2bin(uint8_t wday);
        static uint8_t bin2wday(uint8_t wday);
        uint8_t dec2bcd(uint8_t n);
        static uint8_t bcd2dec(uint8_t n);
};

extern RX8025T RTC_RX8025T;

#endif
