#ifndef MENU_H
#define MENU_H

/*
 * The menu system for the EleksTubeHAX firmware.
 * TODO This system could probably be generalized into a virtual class, but I'm starting
 * with writing it special purpose.
 */
#include <Arduino.h>
#include "Buttons.h"

class Menu
{
public:
  Menu() : state(idle), change(0), millis_last_button_press(0), state_changed(false) {}
  void begin() {}
  void loop(Buttons &buttons);

#ifndef WIFI_USE_WPS
  enum states
  {
    idle = 0,            // idle == out of menu.
    backlight_pattern,   // Change the backlight patterns.
    pattern_color,       // Change the backlight pattern color. TODO pattern speeds?
    backlight_intensity, // Change how bright the backlight LEDs are.
    twelve_hour,         // Select 12 hour or 24 hour format.
    blank_hours_zero,    // Whether to blank the leading zero in the hours column.
    utc_offset_hour,     // Change the UTC offset by an hour.
    utc_offset_15m,      // Change the UTC offset by 15 minutes.
    selected_graphic,    // Select clock "font" 0...9 -> first char in file name "00.bmp to 90.bmp".
    // When there's more things to change in the menu, add them here.
    num_states
  };
#else
  enum states
  {
    idle = 0,            // idle == out of menu.
    backlight_pattern,   // Change the backlight patterns.
    pattern_color,       // Change the backlight pattern color. TODO pattern speeds?
    backlight_intensity, // Change how bright the backlight LEDs are.
    twelve_hour,         // Select 12 hour or 24 hour format.
    blank_hours_zero,    // Whether to blank the leading zero in the hours column.
    utc_offset_hour,     // Change the UTC offset by an hour.
    utc_offset_15m,      // Change the UTC offset by 15 minutes.
    selected_graphic,    // Select clock "font" 0...9 -> first char in file name "00.bmp to 90.bmp".
    start_wps,           // connect to WiFi using wps pushbutton mode
    // When there's more things to change in the menu, add them here.
    num_states
  };
#endif

  const static String state_str[num_states];

  states getState() { return (state); }
  int8_t getChange() { return (change); }

  String getStateStr() { return state_str[state]; }
  bool stateChanged() { return (state_changed); }

private:
  const uint16_t idle_timeout_ms = 10000; // Timeout and return to idle after 10 seconds of inactivity.

  // State variables
  states state;
  int8_t change; // 0 == no action, positive == right button, negative == left button.
                 // For now, these are only +1 and -1. But we might enable acceleration or similar later.
  uint32_t millis_last_button_press;
  bool state_changed; // So we're not redrawing the screen every damn time, signal if the state has changed.
};

#endif // MENU_H
