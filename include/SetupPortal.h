#ifndef SETUP_PORTAL_H
#define SETUP_PORTAL_H

/*
 * Runtime settings + web setup portal (only compiled for the "*_Portal" PlatformIO environments,
 * which define WEB_SETUP_PORTAL).
 *
 * - WiFi credentials: entered on the setup web page; stored by the ESP32 WiFi driver itself.
 * - MQTT broker (Home Assistant): entered on the setup web page; stored in NVS namespace "hax_setup".
 * - Everything else (front LED strip, timezone, night dimming) is changed live from Home Assistant
 *   and also stored in NVS namespace "hax_setup".
 */

#include "GLOBAL_DEFINES.h"

#ifdef WEB_SETUP_PORTAL

#include <Arduino.h>

// Number of backlight LEDs without / with the LED strip along the front/bottom of the clock.
#define HAX_NO_STRIP_LEDS 6
#define HAX_STRIP_LEDS 34 // 6 LEDs behind the LCDs + 28 LEDs in the strip

struct HaxSettings
{
  // --- MQTT broker for Home Assistant (set on the setup web page) ---
  char mqtt_host[64];
  uint16_t mqtt_port;
  char mqtt_user[64];
  char mqtt_pass[64];

  // --- Hardware option (setup web page and Home Assistant) ---
  bool strip_fitted; // LED strip fitted along the front/bottom of the clock?

  // --- Timezone (Home Assistant). The offset itself lives in the clock's stored config. ---
  bool tz_auto; // true: timezone/DST from the IP geolocation lookup, false: offset set manually

  // --- Night dimming (Home Assistant) ---
  bool night_enabled;
  uint8_t night_start_hour;       // 0..23
  uint8_t day_start_hour;         // 0..23
  uint8_t night_screen_level;     // 0..255, brightness of the LCDs at night
  uint8_t night_backlight_level;  // 0..7, intensity of the backlight LEDs at night

  void setDefaults();
  void load();
  void save();
  void markDirty(); // schedule a delayed save (avoids flash writes while a slider is dragged)
  void loop();      // call from the main loop

private:
  bool dirty = false;
  uint32_t dirtyAtMs = 0;
};

extern HaxSettings hax;

// True while the setup portal owns the WiFi radio (the normal reconnect logic must stay out of the way).
extern bool WifiPortalActive;

// True if the WiFi driver has stored credentials. WiFi.mode(WIFI_STA) must have been called before.
bool wifiHasSavedCredentials();

// Opens the setup access point + web page and blocks until the user is done or the timeout expires.
// Restarts the clock when something was saved. Returns only if nothing was configured (timeout / exit).
bool runSetupPortal(bool atBoot);

// Change the number of backlight LEDs while running (switching the front strip on or off).
void applyStripLength(bool fitted);

// Implemented in main.cpp.
void dimmingSettingsChanged();
void geolocRequestUpdateNow();

#endif // WEB_SETUP_PORTAL
#endif // SETUP_PORTAL_H
