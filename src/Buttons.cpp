#include "Buttons.h"

//----------------------------------------------
// Implementation of Button class
// implements all functions for a single button
//----------------------------------------------

void Button::begin()
{
  millis_at_last_transition = millis();
  millis_at_last_loop = millis_at_last_transition;

#ifdef DEBUG_OUTPUT
  Serial.print("init button ");
  Serial.println(bpin);
#endif

  pinMode(bpin, INPUT);

  down_last_time = isButtonDown();
  if (down_last_time)
  {
    button_state = down_edge;
  }
  else
  {
    button_state = idle;
  }
}

void Button::loop()
{
  millis_at_last_loop = millis();
  bool down_now = isButtonDown();

#ifdef DEBUG_OUTPUT
  if (down_now)
  {
    Serial.print("[B ");
    Serial.print(bpin);
    Serial.print("]");
  }
#endif

  state previous_state = button_state;

  if (down_last_time == false && down_now == false)
  {
    // Idle
    button_state = idle;
  }
  else if (down_last_time == false && down_now == true)
  {
    // Just pressed
    button_state = down_edge;
    millis_at_last_transition = millis_at_last_loop;
  }
  else if (down_last_time == true && down_now == true)
  {
    // Been pressed. For how long?
    if (millis_at_last_loop - millis_at_last_transition >= long_press_ms)
    {
      // Long pressed. Did we just transition?
      if (previous_state == down_long_edge || previous_state == down_long)
      {
        // No, we already detected the edge.
        button_state = down_long;
      }
      else
      {
        // Previous state was something else, so this is the transition.
        // down -> down_long_edge does NOT update millis_at_last_transition.
        // We'd rather know how long it's been down than been down_long.
        button_state = down_long_edge;
      }
    }
    else
    {
      // Not yet long pressed
      button_state = down;
    }
  }
  else if (down_last_time == true && down_now == false)
  {
    // Just released.  From how long?
    if (previous_state == down_long_edge || previous_state == down_long)
    {
      // Just released from a long press.
      button_state = up_long_edge;
    }
    else
    {
      // Just released from a short press.
      button_state = up_edge;
    }
    millis_at_last_transition = millis_at_last_loop;
  }

  state_changed = previous_state != button_state;
  down_last_time = down_now;
}

const String Button::state_str[Button::num_states] =
    {"idle",
     "down_edge",
     "down",
     "down_long_edge",
     "down_long",
     "up_edge",
     "up_long_edge"};

//--------------------------------------------
// Implementation of Buttons class
// Superclass for all buttons used (max 4)
//--------------------------------------------

#ifndef ONE_BUTTON_ONLY_MENU
// device has 4 buttons -> define all 4 buttons
void Buttons::begin()
{
  left.begin();
  mode.begin();
  right.begin();
  power.begin();
}

void Buttons::loop()
{
  left.loop();
  mode.loop();
  right.loop();
  power.loop();
}

bool Buttons::stateChanged()
{
  return left.stateChanged() ||
         mode.stateChanged() ||
         right.stateChanged() ||
         power.stateChanged();
}
#endif

#ifdef ONE_BUTTON_ONLY_MENU
// device has one button only -> define "mode" button only
void Buttons::begin()
{
  mode.begin();
}

void Buttons::loop()
{
  mode.loop();
}

bool Buttons::stateChanged()
{
  return mode.stateChanged();
}
#endif