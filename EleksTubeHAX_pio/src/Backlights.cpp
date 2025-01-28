#include "Backlights.h"

void Backlights::begin(StoredConfig::Config::Backlights *config_)
{
  config = config_;

  if (config->is_valid != StoredConfig::valid)
  {
    // Config is invalid, probably a new device never had its config written.
    // Load some reasonable defaults.
    Serial.println("Loaded Backlights config is invalid, using default.  This is normal on first boot.");
    setPattern(rainbow);
    setColorPhase(0);
    setIntensity(max_intensity - 1);
    setPulseRate(60);
    setBreathRate(20);
    setRainbowDuration(DEFAULT_BL_RAINBOW_DURATION_SEC);
    config->is_valid = StoredConfig::valid;
  }
  off = false;
}

// These feel like they should be generalizable into a helper function.
// https://stackoverflow.com/questions/11720656/modulo-operation-with-negative-numbers
void Backlights::setNextPattern(int8_t i)
{
  int8_t next_pattern = (config->pattern + i) % num_patterns;
  while (next_pattern < 0)
  {
    next_pattern += num_patterns;
  }
  setPattern(patterns(next_pattern));
}

void Backlights::adjustColorPhase(int16_t adj)
{
  int16_t new_phase = (int16_t(config->color_phase % max_phase) + adj) % max_phase;
  while (new_phase < 0)
  {
    new_phase += max_phase;
  }
  setColorPhase(new_phase);
}

void Backlights::adjustIntensity(int16_t adj)
{
  int16_t new_intensity = (int16_t(config->intensity) + adj) % max_intensity;
  while (new_intensity < 0)
  {
    new_intensity += max_intensity;
  }
  setIntensity(new_intensity);
}

void Backlights::setIntensity(uint8_t intensity)
{
  config->intensity = intensity;
  setBrightness(0xFF >> max_intensity - config->intensity - 1);
  pattern_needs_init = true;
}

void Backlights::loop()
{
  //   enum patterns { dark, test, constant, rainbow, pulse, breath, num_patterns };
  if (off || config->pattern == dark)
  {
    if (pattern_needs_init)
    {
      clear();
      show();
    }
  }
  else if (config->pattern == test)
  {
    testPattern();
  }
  else if (config->pattern == constant)
  {
    if (pattern_needs_init)
    {
      fill(phaseToColor(config->color_phase));
    }
    if (dimming)
    {
      setBrightness(0xFF >> max_intensity - BACKLIGHT_DIMMED_INTENSITY - 1);
    }
    else
    {
      setBrightness(0xFF >> max_intensity - config->intensity - 1);
    }
    show();
  }
  else if (config->pattern == rainbow)
  {
    rainbowPattern();
  }
  else if (config->pattern == pulse)
  {
    pulsePattern();
  }
  else if (config->pattern == breath)
  {
    breathPattern();
  }

  pattern_needs_init = false;
}

void Backlights::pulsePattern()
{
  fill(phaseToColor(config->color_phase));

  float pulse_length_millis = (60.0f * 1000) / config->pulse_bpm;
  float val = 1 + abs(sin(2 * M_PI * millis() / pulse_length_millis)) * 254;
  if (dimming)
  {
    val = val * BACKLIGHT_DIMMED_INTENSITY / 7;
  }
  else
  {
    val = val * config->intensity / 7;
  }
  setBrightness((uint8_t)val);

  show();
}

void Backlights::breathPattern()
{
  fill(phaseToColor(config->color_phase));

  // https://sean.voisen.org/blog/2011/10/breathing-led-with-arduino/
  // Avoid a 0 value as it shuts off the LEDs and we have to re-initialize.
  float pulse_length_millis = (60.0f * 1000) / config->breath_per_min;
  float val = (exp(sin(2 * M_PI * millis() / pulse_length_millis)) - 0.36787944f) * 108.0f;

  if (dimming)
  {
    val = val * BACKLIGHT_DIMMED_INTENSITY / 7;
  }
  else
  {
    val = val * config->intensity / 7;
  }

  uint8_t brightness = (uint8_t)val;
  if (brightness < 1)
  {
    brightness = 1;
  }
  setBrightness(brightness);

  show();
}

void Backlights::testPattern()
{
  const uint8_t num_colors = 4; // or 3 if you don't want black
  uint8_t num_states = NUM_DIGITS * num_colors;
  uint8_t state = (millis() / test_ms_delay) % num_states;

  uint8_t digit = state / num_colors;
  uint32_t color = 0xFF0000 >> (state % num_colors) * 8;

  clear();
  setPixelColor(digit, color);

  if (dimming)
  {
    setBrightness(0xFF >> max_intensity - (uint8_t)BACKLIGHT_DIMMED_INTENSITY - 1);
  }
  else
  {
    setBrightness(0xFF >> max_intensity - config->intensity - 1);
  }

  show();
}

uint8_t Backlights::phaseToIntensity(uint16_t phase)
{
  uint16_t color = 0;
  if (phase <= 255)
  {
    // Ramping up
    color = phase;
  }
  else if (phase <= 511)
  {
    // Ramping down
    color = 511 - phase;
  }
  else
  {
    // Off
    color = 0;
  }
  if (color > 255)
  {
    // TODO: Trigger ERROR STATE, bug in code.
  }
  return uint8_t(color % 256);
}

uint32_t Backlights::phaseToColor(uint16_t phase)
{
  uint8_t red = phaseToIntensity(phase);
  uint8_t green = phaseToIntensity((phase + 256) % max_phase);
  uint8_t blue = phaseToIntensity((phase + 512) % max_phase);
  return (uint32_t(red) << 16 | uint32_t(green) << 8 | uint32_t(blue));
}

uint32_t Backlights::hueToPhase(float hue)
{
  hue = hue - 120.f;
  if (hue < 0)
  {
    hue = hue + 360.f;
  }
  uint32_t phase = uint32_t(round(768.f * (1.f - hue / 360.f)));
  phase = phase % max_phase;
  return (phase);
}

float Backlights::phaseToHue(uint32_t phase)
{
  float hue = 120.f + ((768.f - float(phase)) / 768.f) * 360.f;
  // h = 120 + (1 - p/768)*360
  if (hue >= 360.f)
  {
    hue = hue - 360.f;
  }
  return (round(hue));
}

void Backlights::rainbowPattern()
{
  // Divide by 3 to spread it out some, so the whole rainbow isn't displayed at once.
  // TODO Make this /3 a parameter
  const uint16_t phase_per_digit = (max_phase / NUM_DIGITS) / 3;

  // Rainbow roatation speed now configurable
  uint16_t duration = uint16_t(round(getRainbowDuration() * 1000));
  uint16_t phase = uint16_t(round(float(millis() % duration) / duration * max_phase));

  for (uint8_t digit = 0; digit < NUM_DIGITS; digit++)
  {
    // Shift the phase for this LED.
    uint16_t my_phase = (phase + digit * phase_per_digit) % max_phase;
    setPixelColor(digit, phaseToColor(my_phase));
  }
  if (dimming)
  {
    #if BACKLIGHT_DIMMED_INTENSITY > 0
      setBrightness(0xFF >> max_intensity - (uint8_t)BACKLIGHT_DIMMED_INTENSITY - 1);
    #else  //turn off backlight if intensity is 0
      setBrightness(0);
    #endif
  }
  else
  {
    setBrightness(0xFF >> max_intensity - config->intensity - 1);
  }
  show();
}

const String Backlights::patterns_str[Backlights::num_patterns] =
    {"Dark", "Test", "Constant", "Rainbow", "Pulse", "Breath"};
