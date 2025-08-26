#ifndef BACKLIGHTS_H
#define BACKLIGHTS_H

#include "GLOBAL_DEFINES.h"

/*
 * A simple sub-class of the Adafruit_NeoPixel class, to configure it for
 * the EleksTube-IPS clock, and to add a little functionality.
 *
 * The good news is, the pixel numbers in the string line up perfectly
 * with the #defines in Hardware.h, so you can pass SECONDS_ONES directly
 * as the pixel index, no mapping required.
 *
 * Otherwise, class Backlights behaves exactly as Adafruit_NeoPixel does.
 */
#include <stdint.h>
#include <math.h>
#include "StoredConfig.h"
#include <Adafruit_NeoPixel.h>

class Backlights : public Adafruit_NeoPixel
{
public:
  Backlights() : config(NULL), pattern_needs_init(true), off(true),
                 Adafruit_NeoPixel(NUM_BACKLIGHT_LEDS, BACKLIGHTS_PIN, NEO_GRB + NEO_KHZ800)
  {
  }

  enum patterns
  {
    dark,
    test,
    constant,
    rainbow,
    pulse,
    breath,
    num_patterns
  };
  const static String patterns_str[num_patterns];

  void begin(StoredConfig::Config::Backlights *config_);
  void loop();

  void togglePower()
  {
    off = !off;
    pattern_needs_init = true;
  }
  void PowerOn()
  {
    off = false;
    pattern_needs_init = true;
  }
  void PowerOff()
  {
    off = true;
    pattern_needs_init = true;
  }
  bool getPower() { return !off; }

  void setPattern(patterns p)
  {
    config->pattern = uint8_t(p);
    pattern_needs_init = true;
  }
  patterns getPattern() { return patterns(config->pattern); }
  String getPatternStr() { return patterns_str[config->pattern]; }
  void setNextPattern(int8_t i = 1);
  void setPrevPattern() { setNextPattern(-1); }

  // Configure the patterns
  void setPulseRate(uint8_t bpm) { config->pulse_bpm = bpm; }
  uint8_t getPulseRate() { return config->pulse_bpm; }
  void setBreathRate(uint8_t per_min) { config->breath_per_min = per_min; }
  uint8_t getBreathRate() { return config->breath_per_min; }
  void setRainbowDuration(float seconds) { config->rainbow_sec = seconds; }
  float getRainbowDuration() { return config->rainbow_sec; }

  // Used by all constant color patterns.
  void setColorPhase(uint16_t phase)
  {
    config->color_phase = phase % max_phase;
    pattern_needs_init = true;
  }
  void adjustColorPhase(int16_t adj);
  uint16_t getColorPhase() { return config->color_phase; }
  uint32_t getColor() { return phaseToColor(config->color_phase); }

  void setIntensity(uint8_t intensity);
  void adjustIntensity(int16_t adj);
  uint8_t getIntensity() { return config->intensity; }

  void setDimming(bool dim)
  {
    dimming = dim;
    pattern_needs_init = true;
  }

  // Helper methods
  uint32_t phaseToColor(uint16_t phase);
  uint32_t hueToPhase(float hue);
  float phaseToHue(uint32_t phase);
  uint8_t phaseToIntensity(uint16_t phase);

  const uint16_t max_phase = 768;  // 256 up, 256 down, 256 off
  const uint8_t max_intensity = 8; // 0 to 7

private:
  bool dimming = false;
  bool pattern_needs_init;
  bool off;

  // Pattern configs, get backed up.
  StoredConfig::Config::Backlights *config;

  // Pattern methods
  void testPattern();
  void rainbowPattern();
  void pulsePattern();
  void breathPattern();

  const uint32_t test_ms_delay = 250;
};

extern Backlights backlights;

#endif // BACKLIGHTS_H
