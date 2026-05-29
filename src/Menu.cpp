#include "Menu.h"

// Big ol' state machine: menu and buttons as the state, buttons as the transition triggers.

#if defined(CAPACITIVE_TOUCH_BUTTONS)
// ─────────────────────────────────────────────────────────────────────────────
// 2-button capacitive touch menu  (D-Esign: LEFT ring = GPIO32, RIGHT = GPIO33)
//
//  Both rings idle                  → nothing
//  Any short tap release (idle)     → wake menu, enter first menu item
//  LEFT  short (up_edge)            → decrement value  (change--)
//  RIGHT short (up_edge)            → increment value  (change++)
//  RIGHT long  (500 ms)             → next menu item
//  LEFT  long  (500 ms, in menu)    → exit to idle
//  LEFT  long  (500 ms, idle)       → toggle display power
//
// The menu wakes on up_edge (release), NOT on down_edge (press). This is
// intentional: it allows a LEFT long-press while idle to be captured by the
// power-toggle handler before the menu ever opens. A long press fires
// down_long_edge after 500 ms and releases via up_long_edge (not up_edge),
// so only a genuine short tap triggers the up_edge that opens the menu.
// ─────────────────────────────────────────────────────────────────────────────
void Menu::loop(Buttons &buttons)
{
  Button::state left_state = buttons.left.getState();
  Button::state right_state = buttons.right.getState();

  change = 0;
  state_changed = false;
  power_toggle = false;

  // Early out for fully idle state.
  if (state == idle && left_state == Button::idle && right_state == Button::idle)
    return;

  // Go idle on timeout (no interaction for idle_timeout_ms).
  if (state != idle && millis() - millis_last_button_press > idle_timeout_ms)
  {
    state = idle;
    state_changed = true;
    return;
  }

  // LEFT long press while idle → toggle display power.
  // Must be checked before the menu-wake handler so the menu never opens.
  if (state == idle && left_state == Button::down_long_edge)
  {
    power_toggle = true;
    return;
  }

  // Wake menu from idle on short release (up_edge).
  if (state == idle && (left_state == Button::up_edge || right_state == Button::up_edge))
  {
    state = states(1); // Start at the beginning of the menu.
    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // LEFT long press while in menu → exit to idle.
  if (state != idle && left_state == Button::down_long_edge)
  {
    state = idle;
    state_changed = true;
    return;
  }

  // RIGHT long press → advance to next menu item.
  if (state != idle && right_state == Button::down_long_edge)
  {
    uint8_t new_state = (uint8_t(state) + 1) % num_states;
    if (new_state == 0)
      new_state = 1; // Skip over idle.
    state = states(new_state);
    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // LEFT short release → decrement value.
  if (state != idle && left_state == Button::up_edge)
  {
    change--;
    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // RIGHT short release → increment value.
  if (state != idle && right_state == Button::up_edge)
  {
    change++;
    millis_last_button_press = millis();
    state_changed = true;
    return;
  }
}

#elif !defined(ONE_BUTTON_ONLY_MENU)
void Menu::loop(Buttons &buttons)
{
  Button::state left_state = buttons.left.getState();   // decrement value
  Button::state right_state = buttons.right.getState(); // increment value
  Button::state power_state = buttons.power.getState(); // exit menu
  Button::state mode_state = buttons.mode.getState();   // next menu

  // Reset the change value in every case.  We don't always change the state though.
  change = 0;
  state_changed = false;

  // Early out for idle state, which will be most of the time.
  if (state == idle && left_state == Button::idle && right_state == Button::idle && mode_state == Button::idle)
  {
    // Everything is idle.
    return;
  }

  // Go idle if the user hasn't pressed a button in a long time.
  if (state != idle && millis() - millis_last_button_press > idle_timeout_ms)
  {
    // Go idle.
    state = idle;
    state_changed = true;
    return;
  }

  // Menu is idle. A button is pressed, go into the menu, but don't act on the button press. It just wakes up the menu.
  if (state == idle && (left_state == Button::down_edge || right_state == Button::down_edge || mode_state == Button::down_edge))
  {
    state = states(1); // Start at the beginning of the menu.

    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // Go to the next menu option
  if (state != idle && mode_state == Button::down_edge)
  {
    uint8_t new_state = (uint8_t(state) + 1) % num_states;
    if (new_state == 0)
    {
      new_state = 1; // Skip over idle when incrementing through the menu.
    }
    state = states(new_state);

    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // Exit with a power button.
  if (state != idle && (power_state == Button::down_edge))
  {
    state = idle;
    state_changed = true;
    return;
  }

  // In a menu, and a left (negative change value) or right button (positive change value) has been pressed!
  if (state != idle && (left_state == Button::down_edge || right_state == Button::down_edge))
  {
    // Pressing both left and right at the same time cancels out?  Sure, why not...
    if (left_state == Button::down_edge)
    {
      change--;
    }
    if (right_state == Button::down_edge)
    {
      change++;
    }

    millis_last_button_press = millis();
    state_changed = true;
    return;
  }
  // Some other button state, but it doesn't trigger any change in state.  There are LOTS of states that will
  // get here, but I think they're all "just do nothing."  If there's an explicit state we want to handle,
  // add an if() block above.
}

#else  // ONE_BUTTON_ONLY_MENU
void Menu::loop(Buttons &buttons)
{
  Button::state mode_state = buttons.mode.getState(); // next menu

  // Reset the change value in every case.  We don't always change the state though.
  change = 0;
  state_changed = false;

  // Early out for idle state, which will be most of the time.
  if (state == idle && mode_state == Button::idle)
  {
    // Everything is idle. Do nothing.
    return;
  }

  // Go idle if the user hasn't pressed a button in a long time.
  if (state != idle && millis() - millis_last_button_press > idle_timeout_ms)
  {
    // Go idle.
    state = idle;
    state_changed = true;
    return;
  }

  // Menu is idle. A button is pressed, go into the menu, but don't act on the button press. It just wakes up the menu.
  if (state == idle && (mode_state == Button::up_edge))
  {
    state = states(1); // Start at the beginning of the menu.

    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // In a menu, and button long pressed! -> simulate right button press
  /// Must be done BEFORE the next menu option
  if (state != idle && (mode_state == Button::down_long_edge))
  {
    change++;

    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // Go to the next menu option
  if (state != idle && mode_state == Button::up_edge)
  {
    uint8_t new_state = (uint8_t(state) + 1) % num_states;
    if (new_state == 0)
    {
      new_state = 1; // Skip over idle when incrementing through the menu.
    }
    state = states(new_state);

    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // Some other button state, but it doesn't trigger any change in state.  There are LOTS of states that will
  // get here, but I think they're all "just do nothing."  If there's an explicit state we want to handle,
  // add an if() block above.
}
#endif // CAPACITIVE_TOUCH_BUTTONS / ONE_BUTTON_ONLY_MENU

// menu has one more point of entry, if WPS is used
#ifndef WIFI_USE_WPS
const String Menu::state_str[Menu::num_states] = {
    "idle",
    "backlight_pattern",
    "pattern_color",
    "backlight_intensity",
    "twelve_hour",
    "blank_hours_zero",
    "utc_offset_hour",
    "utc_offset_15m",
    "selected_graphic"};
#else
const String Menu::state_str[Menu::num_states] = {
    "idle",
    "backlight_pattern",
    "pattern_color",
    "backlight_intensity",
    "twelve_hour",
    "blank_hours_zero",
    "utc_offset_hour",
    "utc_offset_15m",
    "selected_graphic",
    "start_wps"};
#endif
