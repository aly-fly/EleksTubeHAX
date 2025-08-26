#include "ChipSelect.h"

#ifdef HARDWARE_IPSTUBE_CLOCK
// Define the pins for each LCD's enable wire
// The order is from left to right, so the first pin is for the seconds ones, the last for the hours tens
// LCD2 is the leftmost one         - seconds one - pin 21 as GPIO15
// LCD3 is the second from the left - seconds ten - pin 22 as GPIO2
// LCD4 is the third from the left  - minutes one - pin 23 as GPIO27
// LCD5 is the fourth from the left - minutes ten - pin 17 as GPIO14
// LCD6 is the fifth from the left  - hours one   - pin 18 as GPIO12
// LCD7 is the rightmost one        - hours ten   - pin 20 as GPIO13
const int lcdEnablePins[NUM_DIGITS] = {GPIO_NUM_15, GPIO_NUM_2, GPIO_NUM_27, GPIO_NUM_14, GPIO_NUM_12, GPIO_NUM_13};
const int numLCDs = NUM_DIGITS;
#endif

void ChipSelect::begin()
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  pinMode(CSSR_LATCH_PIN, OUTPUT);
  pinMode(CSSR_DATA_PIN, OUTPUT);
  pinMode(CSSR_CLOCK_PIN, OUTPUT);

  digitalWrite(CSSR_DATA_PIN, LOW);
  digitalWrite(CSSR_CLOCK_PIN, LOW);
  digitalWrite(CSSR_LATCH_PIN, LOW);
  update();
#else
  // Initialize all six different pins for the CS of each LCD as OUTPUT and set it to HIGH (disabled)
  for (int i = 0; i < numLCDs; ++i)
  {
    pinMode(lcdEnablePins[i], OUTPUT);
    digitalWrite(lcdEnablePins[i], HIGH);
  }
#endif
}

void ChipSelect::clear(bool update_)
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  setDigitMap(all_off, update_);
#else
  disableAllCSPins();
#endif
}

void ChipSelect::setAll(bool update_)
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  setDigitMap(all_on, update_);
#else
  enableAllCSPins();
#endif
}

void ChipSelect::setDigit(uint8_t digit, bool update_)
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  // Set the bit for the given digit in the digits_map
  setDigitMap(1 << digit, update_);
  if (update_)
    update();
#else
  // Set the actual currentLCD value for the given digit and activate the corresponding LCD

  // first deactivate the current LCD
  disableDigitCSPins(currentLCD);
  // store the current
  currentLCD = digit;
  // activate the new one
  enableDigitCSPins(digit);
  // NO UPDATE, cause update enables and disables the pin, but needs to be "enabled", while eTFT_SPI is writing into it.
#endif
}

void ChipSelect::update()
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  // Documented in README.md.  Q7 and Q6 are unused. Q5 is Seconds Ones, Q0 is Hours Tens.
  // Q7 is the first bit written, Q0 is the last.  So we push two dummy bits, then start with
  // Seconds Ones and end with Hours Tens.
  // CS is Active Low, but digits_map is 1 for enable, 0 for disable.  So we bit-wise NOT first.

  uint8_t to_shift = (~digits_map) << 2;

  digitalWrite(CSSR_LATCH_PIN, LOW);
  shiftOut(CSSR_DATA_PIN, CSSR_CLOCK_PIN, LSBFIRST, to_shift);
  digitalWrite(CSSR_LATCH_PIN, HIGH);
#else
  // this is just, to follow the "update" logic of the other hardware!
  // for IPSTUBE clocks, the CS pin is already pulled to LOW by the "setDigit" function and stays there, till another "setDigit" is called.
  // so all writing done by the eTFT_SPI lib functions in the time, the pin is low, will write out directly to the LCD.
  //"Update" never will work, because, if pin was HIGH, no writing was done.
  digitalWrite(lcdEnablePins[currentLCD], LOW);
#endif
}

bool ChipSelect::isSecondsOnes()
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  return ((digits_map & SECONDS_ONES_MAP) > 0);
#else
  return true;
#endif
}

bool ChipSelect::isSecondsTens()
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  return ((digits_map & SECONDS_TENS_MAP) > 0);
#else
  return true;
#endif
}

bool ChipSelect::isMinutesOnes()
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  return ((digits_map & MINUTES_ONES_MAP) > 0);
#else
  return true;
#endif
}

bool ChipSelect::isMinutesTens()
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  return ((digits_map & MINUTES_TENS_MAP) > 0);
#else
  return true;
#endif
}

bool ChipSelect::isHoursOnes()
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  return ((digits_map & HOURS_ONES_MAP) > 0);
#else
  return true;
#endif
}

bool ChipSelect::isHoursTens()
{
#ifndef HARDWARE_IPSTUBE_CLOCK
  return ((digits_map & HOURS_TENS_MAP) > 0);
#else
  return true;
#endif
}

void ChipSelect::enableAllCSPins()
{
#ifdef HARDWARE_IPSTUBE_CLOCK
  // enable each LCD
  for (int i = 0; i < numLCDs; ++i)
  {
    digitalWrite(lcdEnablePins[i], LOW);
  }
#endif
}

void ChipSelect::disableAllCSPins()
{
#ifdef HARDWARE_IPSTUBE_CLOCK
  // disable each LCD
  for (int i = 0; i < numLCDs; ++i)
  {
    digitalWrite(lcdEnablePins[i], HIGH);
  }
#endif
}

void ChipSelect::enableDigitCSPins(uint8_t digit)
{
#ifdef HARDWARE_IPSTUBE_CLOCK
  // enable the LCD for the given digit
  digitalWrite(lcdEnablePins[digit], LOW);
#endif
}

void ChipSelect::disableDigitCSPins(uint8_t digit)
{
#ifdef HARDWARE_IPSTUBE_CLOCK
  // disable the LCD for the given digit
  digitalWrite(lcdEnablePins[digit], HIGH);
#endif
}
