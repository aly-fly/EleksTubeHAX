#ifndef CHIP_SELECT_H
#define CHIP_SELECT_H

#include "GLOBAL_DEFINES.h"

#ifdef HARDWARE_MARVELTUBESMINI_CLOCK
#include <Wire.h>
#endif

/*
 * `digit`s are as defined in Hardware.h, 0 == seconds ones, 5 == hours tens.
 */

class ChipSelect
{
public:
  ChipSelect() {};

  void begin();
  void update();

  int currentLCD = 0;

  // These speak the indexes defined in Hardware.h.
  // So 0 is disabled, 1 is enabled (even though CS is active low, this gets mapped.)
  // So bit 0 (LSB), is index 0, is SECONDS_ONES
  // Translation to what the 74HC595 uses is done in update()
  void setDigitMap(uint8_t map, bool update_ = true)
  {
    digits_map = map;
    if (update_)
      update();
  }
  uint8_t getDigitMap() { return digits_map; }

  // Helper functions
  // Sets just the one digit by digit number
  void setDigit(uint8_t digit, bool update_ = true);
  void enableDigitCSPins(uint8_t digit);
  void disableDigitCSPins(uint8_t digit);

  void setAll(bool update_ = true);
  void clear(bool update_ = true);

  void setSecondsOnes() { setDigit(SECONDS_ONES); }
  void setSecondsTens() { setDigit(SECONDS_TENS); }
  void setMinutesOnes() { setDigit(MINUTES_ONES); }
  void setMinutesTens() { setDigit(MINUTES_TENS); }
  void setHoursOnes() { setDigit(HOURS_ONES); }
  void setHoursTens() { setDigit(HOURS_TENS); }
  bool isSecondsOnes();
  bool isSecondsTens();
  bool isMinutesOnes();
  bool isMinutesTens();
  bool isHoursOnes();
  bool isHoursTens();

  void enableAllCSPins();
  void disableAllCSPins();

  void reclaimPins();

  // Unified display controller interface
  void setDim(uint32_t duty);
  void setEnabled(bool enabled);

private:
  uint8_t digits_map;
  const uint8_t all_on = 0x3F;
  const uint8_t all_off = 0x00;

#ifdef HARDWARE_MARVELTUBESMINI_CLOCK
  // Fixed address and command definitions for the specific IO expander used on the MarvelTubes Mini.
  // Non-standard custom chip (NOT PCA9554-compatible):
  //   Reg 0x00 = CS select output (0=selected, 1=deselected) — writable
  //   Reg 0x01 = Power/enable register (controlled via init replay sequence)
  //   Reg 0x02 = Dimming/brightness register
  //   Reg 0x03 = Unknown — DO NOT WRITE
  static constexpr uint8_t EXPANDER_ADDR      = 0x19;
  static constexpr uint8_t EXPANDER_CMD_DIGIT = 0x00;
  static constexpr uint8_t EXPANDER_CMD_DIM   = 0x02;
  const uint8_t cs_masks[NUM_DIGITS] = {0x7F, 0xBF, 0xDF, 0xFB, 0xFD, 0xFE};
  void i2cReplayInitSequence(uint8_t address);
#endif
};

#endif // CHIP_SELECT_H
