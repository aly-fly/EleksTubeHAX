#ifndef STORED_CONFIG_H
#define STORED_CONFIG_H

#include "GLOBAL_DEFINES.h"

#include <Preferences.h>
/*
 * TODO: This was originally written for the EEPROM library where all this logic was needed.
 * But Preferences.h does a lot of this itself.  It might make sense to just use Preferences
 * directly, using "Backlights", "Clock", and "Wifi" as the name space in begin(), and
 * "pattern", "color_phase", etc as they key in that name space.
 *
 * The advantage to the way we're doing it now is the name space and keys are checked syntatically
 * at compile time: there's no way to accidentally typo the wrong value, the compiler would catch it.
 * Where-as if we change all that to arbitrary strings, we'd lose that.  So for now, I'm keeping it
 * the way it is. -- @SmittyHalibut
 */

class StoredConfig
{
public:
  StoredConfig() : prefs(), config_size(sizeof(config)), loaded(false), config() {}
  void begin()
  {
    prefs.begin(SAVED_CONFIG_NAMESPACE, false);
    Serial.print("Config size: ");
    Serial.println(config_size);
  }
  void load()
  {
    prefs.getBytes(SAVED_CONFIG_NAMESPACE, &config, config_size);
    loaded = true;
  }
  void save() { prefs.putBytes(SAVED_CONFIG_NAMESPACE, &config, config_size); }
  bool isLoaded() { return loaded; }

  const static uint8_t str_buffer_size = 32;

  struct Config
  {
    struct Backlights
    {
      uint8_t pattern;
      uint16_t color_phase;
      uint8_t intensity;
      uint8_t pulse_bpm;
      uint8_t breath_per_min;
      float rainbow_sec;
      uint8_t is_valid; // Write StoredConfig::valid here when valid data is loaded.
    } backlights;

    struct Clock
    {
      bool twelve_hour;
      time_t time_zone_offset;
      bool blank_hours_zero;
      int8_t selected_graphic;
      uint8_t is_valid; // Write StoredConfig::valid here when valid data is loaded.
    } uclock;

    struct Wifi
    {
      char ssid[str_buffer_size];
      char password[str_buffer_size];
      uint8_t WPS_connected; // Write StoredConfig::valid here when valid data is loaded.
    } wifi;
  } config;

  const static uint8_t valid = 0x55; // neither 0x00 nor 0xFF, signaling loaded config isn't just default data.

private:
  Preferences prefs;
  uint16_t config_size;
  bool loaded;
};

#endif // STORED_CONFIG_H
