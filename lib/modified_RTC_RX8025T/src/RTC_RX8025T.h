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
#include <Wire.h>
#include "../../../include/_USER_DEFINES.h" // User defines (located in the src folder)

#if defined(ARDUINO) && ARDUINO >= 100
#include <Arduino.h>
#else
#include <WProgram.h>
#endif

class RX8025T
{
public:
        RX8025T();

        void init(uint32_t rtcSDA = -1, uint32_t rtcSCL = -1, TwoWire &wireBus = Wire);
        // static time_t get(void); // Must be static to work with setSyncProvider() in the Time library
        time_t get(void);
        uint8_t set(time_t t);

        // static uint8_t read(tmElements_t &tm); // must be static to work with get() and setSyncProvider() in the Time library
        uint8_t read(tmElements_t &tm);
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
        TwoWire *i2cBus; // Pointer to the selected I2C bus

        uint8_t currentStateUIEbit;
        uint8_t wday2bin(uint8_t wday);
        static uint8_t bin2wday(uint8_t wday);
        uint8_t dec2bcd(uint8_t n);
        static uint8_t bcd2dec(uint8_t n);
};

// extern RX8025T RTC_RX8025T;

#endif
