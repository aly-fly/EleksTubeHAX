/*
 * Author: Aljaz Ogrin
 * Project: Alternative firmware for EleksTube IPS clock
 * Original location: https://github.com/aly-fly/EleksTubeHAX
 * Hardware: ESP32
 * Based on: https://github.com/SmittyHalibut/EleksTubeHAX
 */

#include <nvs_flash.h>
#include <stdint.h>
#include <math.h>
#include "GLOBAL_DEFINES.h"
#include "Backlights.h"
#include "Buttons.h"
#include "Clock.h"
#include "Menu.h"
#include "StoredConfig.h"
#include "TFTs.h"
#include "WiFi_WPS.h"

#ifdef GEOLOCATION_ENABLED
#include "IPGeolocation_AO.h"
#endif

#if defined(MQTT_PLAIN_ENABLED) || defined(MQTT_HOME_ASSISTANT)
#include "MQTT_client_ips.h"
#endif

/************************
 *   NovelLife Clone    *
 ************************/
#ifdef HARDWARE_NOVELLIFE_CLOCK
// #include "Gestures.h"
// TODO put into class
#include <Wire.h>
#include <SparkFun_APDS9960.h>

// TODO put into class.
SparkFun_APDS9960 apds = SparkFun_APDS9960();
int volatile isr_flag = 0; // Interrupt signal for gesture sensor

extern Buttons buttons;

void GestureStart();
void HandleGestureInterupt(void);
void GestureInterruptRoutine(void);
void HandleGesture(void);

void GestureStart()
{
  // For gesture sensor APDS9660 set interrupt pin on ESP32 as input.
  pinMode(GESTURE_SENSOR_INPUT_PIN, INPUT);

  // Initialize interrupt service routine for APDS-9960 sensor.
  attachInterrupt(digitalPinToInterrupt(GESTURE_SENSOR_INPUT_PIN), GestureInterruptRoutine, FALLING);

  // Initialize gesture sensor APDS-9960 (configure I2C and initial values).
  if (apds.init())
  {
    Serial.println(F("APDS-9960 initialization complete"));

    // Set Gain to 1x, because the cheap chinese fake APDS sensor can't handle more (also remember to extend ID check in SparkFun libary to 0x3B!).
    apds.setGestureGain(GGAIN_1X);

    // Start running the APDS-9960 gesture sensor engine.
    if (apds.enableGestureSensor(true))
    {
      Serial.println(F("Gesture sensor is now running"));
    }
    else
    {
      Serial.println(F("Something went wrong during gesture sensor enablimg in the APDS-9960 library!"));
    }
  }
  else
  {
    Serial.println(F("Something went wrong during APDS-9960 init!"));
  }
}

// Handle Interrupt from gesture sensor and simulate a short button press (state down_edge) of the corresponding button, if a gesture is detected.
void HandleGestureInterupt()
{
  if (isr_flag == 1)
  {
    detachInterrupt(digitalPinToInterrupt(GESTURE_SENSOR_INPUT_PIN));
    HandleGesture();
    isr_flag = 0;
    attachInterrupt(digitalPinToInterrupt(GESTURE_SENSOR_INPUT_PIN), GestureInterruptRoutine, FALLING);
  }
  return;
}

// Mark that the Interrupt of the gesture sensor was signaled.
void GestureInterruptRoutine()
{
  isr_flag = 1;
  return;
}

// Check which gesture was detected.
void HandleGesture()
{
  // Serial.println("->main::HandleGesture()");
  if (apds.isGestureAvailable())
  {
    switch (apds.readGesture())
    {
    case DIR_UP:
      buttons.left.setDownEdgeState();
      Serial.println("Gesture detected! LEFT");
      break;
    case DIR_DOWN:
      buttons.right.setDownEdgeState();
      Serial.println("Gesture detected! RIGHT");
      break;
    case DIR_LEFT:
      buttons.power.setDownEdgeState();
      Serial.println("Gesture detected! DOWN");
      break;
    case DIR_RIGHT:
      buttons.mode.setDownEdgeState();
      Serial.println("Gesture detected! UP");
      break;
    case DIR_NEAR:
      buttons.mode.setDownEdgeState();
      Serial.println("Gesture detected! NEAR");
      break;
    case DIR_FAR:
      buttons.power.setDownEdgeState();
      Serial.println("Gesture detected! FAR");
      break;
    default:
      Serial.println("Movement detected but NO gesture detected!");
    }
  }
  return;
}
#endif // #ifdef HARDWARE_NOVELLIFE_CLOCK

char UniqueDeviceName[32];      // Enough space for <DeviceName> + 6 hex chars + null
char UniqueDeviceNameLower[32]; // Lowercase variant used for MQTT topics

Backlights backlights;
Buttons buttons;
TFTs tfts;
Clock uclock;
Menu menu;
StoredConfig stored_config;

#ifdef GEOLOCATION_ENABLED
double GeoLocTZoffset = 0;
bool GetGeoLocationTimeZoneOffset();
constexpr uint8_t GEOLOC_MAX_FAILURES_PER_DAY = 4;
constexpr uint32_t GEOLOC_RETRY_BACKOFF_MS = 5UL * 60UL * 1000UL;
uint8_t GeoLocFailedAttempts = 0;
uint32_t GeoLocNextRetryMillis = 0;
uint8_t GeoLocAttemptDay = 0;
bool GeoLocNeedsUpdate = false;
void processGeoLocUpdate(void);
void checkUpdateGeoLocNeeded(void);
uint8_t yesterday = 0;
#endif

#ifdef DIMMING
bool isDimmingNeeded = false;
uint8_t hour_old = 255;
#endif

uint32_t lastMQTTCommandExecuted = (uint32_t)-1;

// Helper function, defined below.
void updateClockDisplay(TFTs::show_t show = TFTs::yes);
void setupMenu(void);
#ifdef DIMMING
bool isNightTime(uint8_t current_hour);
void checkDimmingNeeded(void);
#endif

//-----------------------------------------------------------------------
// Setup
//-----------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(1500); // Wait for serial monitor to catch up

  Serial.println("\nSystem starting...\n");
  Serial.println("EleksTubeHAX https://github.com/aly-fly/EleksTubeHAX");
  Serial.printf("Firmware version: v%s.\n", FIRMWARE_VERSION);

  // Prepare unique device name
#ifdef MQTT_CLIENT_ID_FOR_SMARTNEST
  snprintf(UniqueDeviceName, sizeof(UniqueDeviceName), "%s", MQTT_CLIENT_ID_FOR_SMARTNEST); // Use fixed ID for smartnest.cz
#else
  {
    // Generate unique ID for other cases
    // ESP.getEfuseMac() returns the 48-bit MAC address in the lower bits of the value (little-endian byte order when viewed as an integer).
    // We extract all 6 bytes and store them in the canonical MAC order (MSB first).
    uint64_t rawmac = ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL; // Keep only lower 48 bits, comes in reverse order
    uint8_t mac_bytes[6];
    for (int i = 0; i < 6; i++)
    {
      mac_bytes[i] = (rawmac >> (8 * i)) & 0xFF; // LSB first
    }
    // Generate a unique device name using the last 3 bytes of the MAC address to make the name shorter but still unique enough to avoid collisions
    snprintf(UniqueDeviceName, sizeof(UniqueDeviceName), "%s-%02X%02X%02X", DEVICE_NAME,
             mac_bytes[3], mac_bytes[4], mac_bytes[5]);
  }
#endif // #ifdef MQTT_CLIENT_ID_FOR_SMARTNEST
  // Prepare lowercase variant for MQTT topic usage
  for (size_t i = 0; i < sizeof(UniqueDeviceName); ++i)
  {
    char c = UniqueDeviceName[i];
    UniqueDeviceNameLower[i] = (char)tolower((int)c);
    if (c == '\0')
      break;
  }
  Serial.printf("Set device name: \"%s\" (lowercase: %s).\n", UniqueDeviceName, UniqueDeviceNameLower);

  Serial.print("Init NVS flash partition usage...");
  esp_err_t ret = nvs_flash_init(); // Initialize NVS
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    Serial.println("");
    Serial.println("No free pages in or newer version of NVS partition found. Erasing NVS flash partition...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  Serial.println("Done.");

  stored_config.begin();
  stored_config.load();

  backlights.begin(&stored_config.config.backlights);
  buttons.begin();
  menu.begin();

  // Setup the displays (TFTs) initaly and show bootup message(s).
  tfts.begin(); // ...and count number of clock faces available...
  tfts.fillScreen(TFT_BLACK);
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.setCursor(0, 0, 2); // Font 2. 16 pixel high
  tfts.println("Starting Setup...");

#ifdef HARDWARE_NOVELLIFE_CLOCK
  // Init the Gesture sensor
  tfts.setTextColor(TFT_ORANGE, TFT_BLACK);
  tfts.print("Gest start...");
  Serial.print("Gesture Sensor start...");
  GestureStart(); // TODO put into class
  tfts.println("Done!");
  Serial.println("Done!");
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
#endif // #ifdef HARDWARE_NOVELLIFE_CLOCK

  // Setup WiFi connection. Must be done before setting up Clock.
  // This is done outside Clock so the network can be used for other things.
  tfts.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
  tfts.println("WiFi start...");
  Serial.println("WiFi start...");
  WifiBegin();
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);

  // Wait a bit (5x100ms = 0.5 sec) before querying NTP.
  for (uint8_t ndx = 0; ndx < 5; ndx++)
  {
    tfts.print(">");
    delay(100);
  }
  tfts.println("");

  // Setup the clock. It needs WiFi to be established already.
  tfts.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tfts.print("Clock start...");
  Serial.println("\nClock start-up...");
  uclock.begin(&stored_config.config.uclock);
  tfts.println("Done!");
  Serial.println("\nClock start-up done!");
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);

#if defined(MQTT_PLAIN_ENABLED) || defined(MQTT_HOME_ASSISTANT)
  // Setup MQTT.
  tfts.setTextColor(TFT_YELLOW, TFT_BLACK);
  tfts.print("MQTT start...");
  Serial.println("\nMQTT start...");
  MQTTStart(false);
  tfts.println("Done!");
  Serial.println("MQTT start Done!");
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
#endif

#ifdef GEOLOCATION_ENABLED
  tfts.setTextColor(TFT_CYAN, TFT_BLACK);
  tfts.println("GeoLoc query...");
  if (GetGeoLocationTimeZoneOffset())
  {
    tfts.print("TZ: ");
    Serial.print("TZ: ");
    tfts.println(GeoLocTZoffset);
    Serial.println(GeoLocTZoffset);
    uclock.setTimeZoneOffset(GeoLocTZoffset * 3600);
    Serial.println();
    Serial.print("Saving config! Triggered by timezone change...");
    stored_config.save();
    tfts.println("Done!");
    Serial.println("Done!");
    tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  else
  {
    tfts.setTextColor(TFT_RED, TFT_BLACK);
    tfts.println("GeoLoc FAILED");
    Serial.println("GeoLoc failed!");
    tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  }
#endif

  if (uclock.getActiveGraphicIdx() > tfts.NumberOfClockFaces)
  {
    uclock.setActiveGraphicIdx(tfts.NumberOfClockFaces);
    Serial.println("Last selected index of clock face is larger than currently available number of image sets.");
  }
  if (uclock.getActiveGraphicIdx() < 1)
  {
    uclock.setActiveGraphicIdx(1);
    Serial.println("Last selected index of clock face is less than 1.");
  }
  tfts.current_graphic = uclock.getActiveGraphicIdx();

  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.println("Done with Setup!");
  Serial.println("\nDone with Setup!");

  // Leave bootup messages on screen for a few seconds (10x200ms = 2 sec).
  for (uint8_t ndx = 0; ndx < 10; ndx++)
  {
    tfts.print(">");
    delay(200);
  }

  // Start up the clock displays.
  tfts.fillScreen(TFT_BLACK);
  uclock.loop();
  updateClockDisplay(TFTs::force); // Draw all the clock digits
  Serial.println("Starting main loop...");
}

//-----------------------------------------------------------------------
// Main loop
//-----------------------------------------------------------------------
void loop()
{
  uint32_t millis_at_top = millis();

  // Do all the maintenance work.
  WifiReconnect(); // If not connected to WiFi, attempt to reconnect

#if defined(MQTT_PLAIN_ENABLED) || defined(MQTT_HOME_ASSISTANT)
  MQTTLoopFrequently();

  bool MQTTCommandReceived =
      MQTTCommandMainPowerReceived ||
      MQTTCommandBackPowerReceived ||
      MQTTCommandStateReceived ||
      MQTTCommandBrightnessReceived ||
      MQTTCommandMainBrightnessReceived ||
      MQTTCommandBackBrightnessReceived ||
      MQTTCommandPatternReceived ||
      MQTTCommandBackPatternReceived ||
      MQTTCommandBackColorPhaseReceived ||
      MQTTCommandGraphicReceived ||
      MQTTCommandMainGraphicReceived ||
      MQTTCommandUseTwelveHoursReceived ||
      MQTTCommandBlankZeroHoursReceived ||
      MQTTCommandPulseBpmReceived ||
      MQTTCommandBreathBpmReceived ||
      MQTTCommandRainbowSecReceived;

  if (MQTTCommandMainPowerReceived)
  {
    MQTTCommandMainPowerReceived = false;
    if (MQTTCommandMainPower)
    {
      // Perform reinit, enable, redraw only if displays are actually off. HA sends ON command together with clock face change which causes flickering.
      if (!tfts.isEnabled())
      {
#ifdef HARDWARE_ELEKSTUBE_CLOCK // Original EleksTube hardware and direct clones need a reinit to wake up the displays properly
        tfts.reinit();
#else
        tfts.enableAllDisplays(); // For all other clocks, just enable the displays
#endif
        updateClockDisplay(TFTs::force); // Redraw all the clock digits; needed because the displays was blanked before turning off
      }
    }
    else
    {
      tfts.chip_select.setAll();
      tfts.fillScreen(TFT_BLACK); // Blank the screens before turning off; needed for all clocks without a real power switch circuit to "simulate" the switched-off displays
      tfts.disableAllDisplays();
    }
  }

  if (MQTTCommandBackPowerReceived)
  {
    MQTTCommandBackPowerReceived = false;
    if (MQTTCommandBackPower)
    {
      backlights.PowerOn();
    }
    else
    {
      backlights.PowerOff();
    }
  }

  if (MQTTCommandStateReceived)
  {
    MQTTCommandStateReceived = false;
    randomSeed(millis());
    uint8_t idx;
    if (MQTTCommandState >= 90)
    {
      idx = random(1, tfts.NumberOfClockFaces + 1);
    }
    else
    {
      idx = (MQTTCommandState / 5) - 1;
    } // 10..40 -> graphic 1..6
    Serial.print("Graphic change request from MQTT; command: ");
    Serial.print(MQTTCommandState);
    Serial.print(", index: ");
    Serial.println(idx);
    uclock.setClockGraphicsIdx(idx);
    tfts.current_graphic = uclock.getActiveGraphicIdx();
    updateClockDisplay(TFTs::force); // Redraw everything
  }

  if (MQTTCommandMainBrightnessReceived)
  {
    MQTTCommandMainBrightnessReceived = false;
    tfts.dimming = MQTTCommandMainBrightness;
    tfts.ProcessUpdatedDimming();
    updateClockDisplay(TFTs::force);
  }

  if (MQTTCommandBackBrightnessReceived)
  {
    MQTTCommandBackBrightnessReceived = false;
    backlights.setIntensity(uint8_t(MQTTCommandBackBrightness));
  }

  if (MQTTCommandPatternReceived)
  {
    MQTTCommandPatternReceived = false;

    for (int8_t i = 0; i < Backlights::num_patterns; i++)
    {
      Serial.print("New pattern ");
      Serial.print(MQTTCommandPattern);
      Serial.print(", check pattern ");
      Serial.println(Backlights::patterns_str[i]);
      if (strcmp(MQTTCommandPattern, (Backlights::patterns_str[i]).c_str()) == 0)
      {
        backlights.setPattern(Backlights::patterns(i));
        break;
      }
    }
  }

  if (MQTTCommandBackPatternReceived)
  {
    MQTTCommandBackPatternReceived = false;
    for (int8_t i = 0; i < Backlights::num_patterns; i++)
    {
      Serial.print("new pattern ");
      Serial.print(MQTTCommandBackPattern);
      Serial.print(", check pattern ");
      Serial.println(Backlights::patterns_str[i]);
      if (strcmp(MQTTCommandBackPattern, (Backlights::patterns_str[i]).c_str()) == 0)
      {
        backlights.setPattern(Backlights::patterns(i));
        break;
      }
    }
  }

  if (MQTTCommandBackColorPhaseReceived)
  {
    MQTTCommandBackColorPhaseReceived = false;

    backlights.setColorPhase(MQTTCommandBackColorPhase);
  }

  if (MQTTCommandGraphicReceived)
  {
    MQTTCommandGraphicReceived = false;

    uclock.setClockGraphicsIdx(MQTTCommandGraphic);
    tfts.current_graphic = uclock.getActiveGraphicIdx();
    updateClockDisplay(TFTs::force); // Redraw everything
  }

  if (MQTTCommandMainGraphicReceived)
  {
    MQTTCommandMainGraphicReceived = false;
    uclock.setClockGraphicsIdx(MQTTCommandMainGraphic);
    tfts.current_graphic = uclock.getActiveGraphicIdx();
    updateClockDisplay(TFTs::force); // Redraw everything
  }

  if (MQTTCommandUseTwelveHoursReceived)
  {
    MQTTCommandUseTwelveHoursReceived = false;
    uclock.setTwelveHour(MQTTCommandUseTwelveHours);
  }

  if (MQTTCommandBlankZeroHoursReceived)
  {
    MQTTCommandBlankZeroHoursReceived = false;
    uclock.setBlankHoursZero(MQTTCommandBlankZeroHours);
  }

  if (MQTTCommandPulseBpmReceived)
  {
    MQTTCommandPulseBpmReceived = false;
    backlights.setPulseRate(MQTTCommandPulseBpm);
  }

  if (MQTTCommandBreathBpmReceived)
  {
    MQTTCommandBreathBpmReceived = false;
    backlights.setBreathRate(MQTTCommandBreathBpm);
  }

  if (MQTTCommandRainbowSecReceived)
  {
    MQTTCommandRainbowSecReceived = false;
    backlights.setRainbowDuration(MQTTCommandRainbowSec);
  }

  MQTTStatusMainPower = tfts.isEnabled();
  MQTTStatusBackPower = backlights.getPower();
  MQTTStatusState = (uclock.getActiveGraphicIdx() + 1) * 5; // 10
  MQTTStatusBrightness = backlights.getIntensity();
  MQTTStatusMainBrightness = tfts.dimming;
  MQTTStatusBackBrightness = backlights.getIntensity();
  strcpy(MQTTStatusPattern, backlights.getPatternStr().c_str());
  strcpy(MQTTStatusBackPattern, backlights.getPatternStr().c_str());
  backlights.getPatternStr().toCharArray(MQTTStatusBackPattern, backlights.getPatternStr().length() + 1);
  MQTTStatusBackColorPhase = backlights.getColorPhase();
  MQTTStatusGraphic = uclock.getActiveGraphicIdx();
  MQTTStatusMainGraphic = uclock.getActiveGraphicIdx();
  MQTTStatusUseTwelveHours = uclock.getTwelveHour();
  MQTTStatusBlankZeroHours = uclock.getBlankHoursZero();
  MQTTStatusPulseBpm = backlights.getPulseRate();
  MQTTStatusBreathBpm = backlights.getBreathRate();
  MQTTStatusRainbowSec = backlights.getRainbowDuration();

  if (MQTTCommandReceived)
  {
    lastMQTTCommandExecuted = millis();

    MQTTReportBackEverything(true);
  }

  if (lastMQTTCommandExecuted != -1)
  {
    if (((millis() - lastMQTTCommandExecuted) > (MQTT_SAVE_PREFERENCES_AFTER_SEC * 1000)) && menu.getState() == Menu::idle)
    { // Save the config after a while (default is 60 seconds) if no new MQTT command was received and we are not in the menu.
      lastMQTTCommandExecuted = -1;

      Serial.print("Saving config...");
      stored_config.save();
      Serial.println(" Done.");
    }
  }
#endif

  buttons.loop();

#ifdef HARDWARE_NOVELLIFE_CLOCK
  HandleGestureInterupt();
#endif // #ifdef HARDWARE_NOVELLIFE_CLOCK

// if the device has one button only, no power button functionality is needed!
#ifndef ONE_BUTTON_ONLY_MENU
  // Power button: If in menu, exit menu. Else turn off displays and backlight.
  if (buttons.power.isDownEdge() && (menu.getState() == Menu::idle))
  { // Power button was pressed: if in the menu, exit menu, else turn off displays and backlight.
    if (tfts.isEnabled())
    { // Check if TFT state is enabled and switch OFF the LCDs and LED backlights.
      tfts.chip_select.setAll();
      tfts.fillScreen(TFT_BLACK); // Blank the screens before turning off; needed for all clocks without a real power switch circuit
      tfts.disableAllDisplays();
      backlights.PowerOff();
    }
    else
    {                           // TFT state is disabled, turn ON the displays and backlights
#ifdef HARDWARE_ELEKSTUBE_CLOCK // Original EleksTube hardware and direct clones need a reinit to wake up the displays properly
      tfts.reinit();
#else
      tfts.enableAllDisplays(); // For all other clocks, just enable the displays
#endif
      updateClockDisplay(TFTs::force); // Redraw all the clock digits; needed because the displays was blanked before turning off
      backlights.PowerOn();
    }
  }
#endif // ONE_BUTTON_ONLY_MENU

  menu.loop(buttons); // Must be called after buttons.loop()
  backlights.loop();
  uclock.loop();

#ifdef DIMMING
  checkDimmingNeeded(); // Night or day time brightness change
#endif

  updateClockDisplay(); // Draw only the changed clock digits!

#ifdef GEOLOCATION_ENABLED
  checkUpdateGeoLocNeeded(); // Check if it is time to update geolocation based timezone offset (just once per day)
#endif

  // Menu
  if (menu.stateChanged() && tfts.isEnabled())
  {
    Menu::states menu_state = menu.getState();
    int8_t menu_change = menu.getChange();

    if (menu_state == Menu::idle)
    {
      // We just changed into idle, so force a redraw of all clock digits and save the config.
      updateClockDisplay(TFTs::force); // Redraw everything
      Serial.println();
      Serial.print("Saving config! Triggered from leaving menu...");
      stored_config.save();
      Serial.println(" Done.");
    }
    else
    {
      // Backlight Pattern
      if (menu_state == Menu::backlight_pattern)
      {
        if (menu_change != 0)
        {
          backlights.setNextPattern(menu_change);
        }
        setupMenu();
        tfts.println("Pattern:");
        tfts.println(backlights.getPatternStr());
      }
      // Backlight Color
      else if (menu_state == Menu::pattern_color)
      {
        if (menu_change != 0)
        {
          backlights.adjustColorPhase(menu_change * 16);
        }
        setupMenu();
        tfts.println("Color:");
        tfts.printf("%06X\n", backlights.getColor());
      }
      // Backlight Intensity
      else if (menu_state == Menu::backlight_intensity)
      {
        if (menu_change != 0)
        {
          backlights.adjustIntensity(menu_change);
        }
        setupMenu();
        tfts.println("Intensity:");
        tfts.println(backlights.getIntensity());
      }
      // 12 Hour or 24 Hour mode?
      else if (menu_state == Menu::twelve_hour)
      {
        if (menu_change != 0)
        {
          uclock.toggleTwelveHour();
          tfts.setDigit(HOURS_TENS, uclock.getHoursTens(), TFTs::force);
          tfts.setDigit(HOURS_ONES, uclock.getHoursOnes(), TFTs::force);
        }
        setupMenu();
        tfts.println("Hour format");
        tfts.println(uclock.getTwelveHour() ? "12 hour" : "24 hour");
      }
      // Blank leading zeros on the hours?
      else if (menu_state == Menu::blank_hours_zero)
      {
        if (menu_change != 0)
        {
          uclock.toggleBlankHoursZero();
          tfts.setDigit(HOURS_TENS, uclock.getHoursTens(), TFTs::force);
        }
        setupMenu();
        tfts.println("Blank zero?");
        tfts.println(uclock.getBlankHoursZero() ? "yes" : "no");
      }
      // UTC Offset, hours
      else if (menu_state == Menu::utc_offset_hour)
      {
        time_t currOffset = uclock.getTimeZoneOffset();

        if (menu_change != 0)
        {
          // calculate the new offset
          time_t newOffsetAdjustmentValue = menu_change * 3600;
          time_t newOffset = currOffset + newOffsetAdjustmentValue;

          // check if the new offset is within the allowed range of -12 to +12 hours
          // If the minutes part of the offset is 0, we want to change from +12 to -12 or vice versa (without changing the shown time on the displays)
          // If the minutes part is not 0: We want to wrap around to the other side and change the minutes part (i.e. from 11:45 directly to -11:15)
          bool offsetWrapAround = false;
          if (newOffset > 43200)
          { // we just "passed" +12 hours -> set to -12 hours
            newOffset = -43200;
            offsetWrapAround = true;
          }
          if (newOffset < -43200 && !offsetWrapAround)
          { // we just passed -12 hours -> set to +12 hours
            newOffset = 43200;
          }

          uclock.setTimeZoneOffset(newOffset); // set the new offset
          uclock.loop();                       // update the clock time and redraw the changed digits -> will "flicker" the menu for a short time, but without, menu is not redrawn correctly
#ifdef DIMMING
          checkDimmingNeeded(); // check if we need dimming for the night, because timezone was changed
#endif
          currOffset = uclock.getTimeZoneOffset(); // get the new offset as current offset for the menu
        }
        setupMenu();
        tfts.println("UTC Offset");
        tfts.println(" +/- Hour");
        char offsetStr[11];
        int8_t offset_hour = currOffset / 3600;
        int8_t offset_min = (currOffset % 3600) / 60;
        if (offset_min <= 0 && offset_hour <= 0)
        { // negative timezone value -> Make them positive and print a minus in front
          offset_min = -offset_min;
          offset_hour = -offset_hour;
          snprintf(offsetStr, sizeof(offsetStr), "-%d:%02d", offset_hour, offset_min);
        }
        else
        {
          if (offset_min >= 0 && offset_hour >= 0)
          { // postive timezone value for hours and minutes -> show a plus in front
            snprintf(offsetStr, sizeof(offsetStr), "+%d:%02d", offset_hour, offset_min);
          }
        }
        if (offset_min == 0 && offset_hour == 0)
        { // we don't want a sign in front of the 0:00 case
          snprintf(offsetStr, sizeof(offsetStr), "%d:%02d", offset_hour, offset_min);
        }
        tfts.println(offsetStr);
      } // END UTC Offset, hours
      // BEGIN UTC Offset, 15 minutes
      else if (menu_state == Menu::utc_offset_15m)
      {
        time_t currOffset = uclock.getTimeZoneOffset();

        if (menu_change != 0)
        {
          time_t newOffsetAdjustmentValue = menu_change * 900; // calculate the new offset
          time_t newOffset = currOffset + newOffsetAdjustmentValue;

          // check if the new offset is within the allowed range of -12 to +12 hours
          // same behaviour as for the +/-1 hour offset, but with 15 minutes steps
          bool offsetWrapAround = false;
          if (newOffset > 43200)
          { // we just "passed" +12 hours -> set to -12 hours
            newOffset = -43200;
            offsetWrapAround = true;
          }
          if (newOffset < -43200 && !offsetWrapAround)
          { // we just passed -12 hours -> set to +12 hours
            newOffset = 43200;
          }

          uclock.setTimeZoneOffset(newOffset); // set the new offset
          uclock.loop();                       // update the clock time and redraw the changed digits -> will "flicker" the menu for a short time, but without, menu is not redrawn correctly
#ifdef DIMMING
          checkDimmingNeeded(); // check if we need dimming for the night, because timezone was changed
#endif
          currOffset = uclock.getTimeZoneOffset(); // get the new offset as current offset for the menu
        }
        setupMenu();
        tfts.println("UTC Offset");
        tfts.println(" +/- 15m");
        char offsetStr[11];
        int8_t offset_hour = currOffset / 3600;
        int8_t offset_min = (currOffset % 3600) / 60;
        if (offset_min <= 0 && offset_hour <= 0)
        { // negative timezone value -> Make them positive and print a minus in front
          offset_min = -offset_min;
          offset_hour = -offset_hour;
          snprintf(offsetStr, sizeof(offsetStr), "-%d:%02d", offset_hour, offset_min);
        }
        else
        {
          if (offset_min >= 0 && offset_hour >= 0)
          { // postive timezone value for hours and minutes -> show a plus in front
            snprintf(offsetStr, sizeof(offsetStr), "+%d:%02d", offset_hour, offset_min);
          }
        }
        if (offset_min == 0 && offset_hour == 0)
        { // we don't want a sign in front of the 0:00 case so overwrite the string
          snprintf(offsetStr, sizeof(offsetStr), "%d:%02d", offset_hour, offset_min);
        }
        tfts.println(offsetStr);
      } // END UTC Offset, 15 minutes
      // select clock face
      else if (menu_state == Menu::selected_graphic)
      {
        if (menu_change != 0)
        {
          uclock.adjustClockGraphicsIdx(menu_change);

          if (tfts.current_graphic != uclock.getActiveGraphicIdx())
          {
            tfts.current_graphic = uclock.getActiveGraphicIdx();
            updateClockDisplay(TFTs::force); // Redraw everything
          }
        }
        setupMenu();
        tfts.println("Selected");
        tfts.println("graphic:");
        tfts.printf("    %d\n", uclock.getActiveGraphicIdx());
      }
#ifdef WIFI_USE_WPS //  WPS code
      // connect to WiFi using wps pushbutton mode
      else if (menu_state == Menu::start_wps)
      {
        if (menu_change != 0)
        { // button was pressed
          if (menu_change < 0)
          { // left button
            Serial.println("WiFi WPS start request");
            tfts.clear();
            tfts.fillScreen(TFT_BLACK);
            tfts.setTextColor(TFT_WHITE, TFT_BLACK);
            tfts.setCursor(0, 0, 4); // Font 4. 26 pixel high
            WiFiStartWps();
          }
        }
        setupMenu();
        tfts.println("Connect to WiFi?");
        tfts.println("Left=WPS");
      }
#endif
    }
  } // if (menu.stateChanged())

  uint32_t time_in_loop = millis() - millis_at_top;
  if (time_in_loop < 20)
  {
    // we have free time (loop run took under 20 ms), spend it for loading next image into buffer
    tfts.LoadNextImage();

    // Do we still have extra time? -> normally not in the same loop where image loading was done, but in the next loops
    time_in_loop = millis() - millis_at_top;
    if (time_in_loop < 20)
    {
#if defined(MQTT_PLAIN_ENABLED) || defined(MQTT_HOME_ASSISTANT)
      MQTTLoopInFreeTime(); // do less time critical MQTT tasks
#endif

#ifdef GEOLOCATION_ENABLED
      processGeoLocUpdate();
#endif // GEOLOCATION_ENABLED
      // Sleep for up to 20ms, less if we've spent time doing stuff above.
      time_in_loop = millis() - millis_at_top;
      if (time_in_loop < 20) // loop was faster than 20ms -> unusually fast, yield some time to other tasks
      {
        delay(20 - time_in_loop);
      }
    }
  }
#ifdef DEBUG_OUTPUT
  if (time_in_loop <= 2) // if the loop time is less than 2ms, we don't need to print it in detail
    Serial.print(".");
  else
  {
    Serial.print("time spent in loop (ms): "); // print the time spent in the loop
    Serial.println(time_in_loop);
  }
#endif // DEBUG_OUTPUT
}

void setupMenu()
{                                  // Prepare drawing of the menu texts
  tfts.chip_select.setHoursTens(); // use most left display
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.fillRect(0, 120, 135, 120, TFT_BLACK); // use lower half of the display, fill with black
  tfts.setCursor(0, 124, 4);                  // use font 4 - 26 pixel high - for the menu text
}

#ifdef DIMMING
bool isNightTime(uint8_t current_hour)
{ // check the actual hour is in the defined "night time"
  if (DAY_TIME < NIGHT_TIME)
  { // "Night" spans across midnight so it is split between two days
    return (current_hour < DAY_TIME) || (current_hour >= NIGHT_TIME);
  }
  else
  { // "Night" starts after midnight, entirely contained within the current day
    return (current_hour >= NIGHT_TIME) && (current_hour < DAY_TIME);
  }
}

void checkDimmingNeeded()
{                                             // dim the display in the defined night time
  uint8_t current_hour = uclock.getHour24();  // for internal calcs we always use 24h format
  isDimmingNeeded = current_hour != hour_old; // check, if the hour has changed since last loop (from time passing by or from timezone change)
  if (isDimmingNeeded)
  {
    Serial.print("Current hour = ");
    Serial.print(current_hour);
    Serial.print(", Night Time Start = ");
    Serial.print(NIGHT_TIME);
    Serial.print(", Day Time Start = ");
    Serial.println(DAY_TIME);
    if (isNightTime(current_hour))
    { // check if it is in the defined night time
      Serial.println("Set to night time mode (dimmed)!");
      tfts.dimming = TFT_DIMMED_INTENSITY;
      tfts.ProcessUpdatedDimming();
      backlights.setDimming(true);
    }
    else
    {
      Serial.println("Set to day time mode (max brightness)!");
      tfts.dimming = 255; // 0..255
      tfts.ProcessUpdatedDimming();
      backlights.setDimming(false);
    }
    updateClockDisplay(TFTs::force); // Redraw everything; software dimming will be done here
    hour_old = current_hour;
  }
}
#endif // DIMMING

#ifdef GEOLOCATION_ENABLED
bool GetGeoLocationTimeZoneOffset()
{
  Serial.println("\nStarting Geolocation API query...");

#ifdef GEOLOCATION_PROVIDER_IPAPI
  // Use IP-API.com -> Free tier has a 45 requests per minute limit!
  IPGeolocation location(GEOLOCATION_API_KEY, "IPAPI");
#elif defined(GEOLOCATION_PROVIDER_IPGEOLOCATION)
  // Use ipgeolocation.io -> Free tier has 1,000 requests per month limit!
  IPGeolocation location(GEOLOCATION_API_KEY, "IPGEOLOCATION");
#elif defined(GEOLOCATION_PROVIDER_ABSTRACTAPI)
  // Use AbstractAPI.com -> Free tier has 1,000 requests AT ALL per account!
  IPGeolocation location(GEOLOCATION_API_KEY, "ABSTRACTAPI");
#else
  // No provider defined -> default to IP-API.com
  IPGeolocation location(GEOLOCATION_API_KEY, "IPAPI");
#endif

  IPGeo ipg;
  if (location.updateStatus(&ipg))
  {
    Serial.println(String("Geo Time Zone: ") + String(ipg.tz));
    Serial.println(String("Geo TZ Offset: ") + String(ipg.offset));          // primary value of interest
    Serial.println(String("Geo Current Time: ") + String(ipg.current_time)); // currently unused but handy for debugging
    const double rawOffsetHours = ipg.offset;
    const int32_t newOffsetSeconds = static_cast<int32_t>(lround(rawOffsetHours * 3600.0));

    if ((newOffsetSeconds % (15 * 60)) != 0)
    {
      Serial.print("GeoLoc rejected offset not aligned to 15 min grid (seconds): ");
      Serial.println(newOffsetSeconds);
      return false;
    }

    const bool hasValidStoredOffset = stored_config.config.uclock.is_valid == StoredConfig::valid;
    const int32_t previousOffsetSeconds = static_cast<int32_t>(stored_config.config.uclock.time_zone_offset);
    const int32_t defaultOffsetSeconds = 1 * 3600;

    if (hasValidStoredOffset && previousOffsetSeconds != 0 && previousOffsetSeconds != defaultOffsetSeconds)
    {
      int32_t diff = newOffsetSeconds - previousOffsetSeconds;
      if (diff < 0)
      {
        diff = -diff;
      }

      if (diff > (2 * 3600)) // more than 2 hours difference -> reject
      {
        Serial.print("GeoLoc offset deviates by more than 2h from stored value (prev: ");
        Serial.print(previousOffsetSeconds);
        Serial.print("s, new: ");
        Serial.print(newOffsetSeconds);
        Serial.println("s). Ignoring update.");
        return false;
      }
    }

    GeoLocTZoffset = static_cast<double>(newOffsetSeconds) / 3600.0;
    Serial.println(String("Geo TZ Offset (applied): ") + String(GeoLocTZoffset));
    return true;
  }

  Serial.println("Geolocation failed.");
  return false;
}
#endif

#ifdef GEOLOCATION_ENABLED
void checkUpdateGeoLocNeeded()
{
  uint8_t currentDay = uclock.getDay(); // Get current day of month
  const uint8_t currentWeekday = weekday(uclock.loop_time);
  const bool isSunday = (currentWeekday == 1); // TimeLib defines Sunday as weekday 1

  // Only on Sundays, and only if the day has changed since last successful update
  // `isGeoLocWindow` is set to true between 03:00:05 and 03:00:59, because daylight saving time changes usually happen at 03:00 local time.
  // GeoLoc update mechanism in the main 'loop free time slot' is then triggered, because `GeoLocNeedsUpdate` is set to true here.
  const bool isGeoLocWindow = isSunday && (currentDay != yesterday) && (uclock.getHour24() == 3) && (uclock.getMinute() == 0) && (uclock.getSecond() > 5);

  if (!GeoLocNeedsUpdate && isGeoLocWindow)
  {
    Serial.print("GeoLoc needs update! Current date (DD.MM.YYYY): ");
    Serial.print(currentDay);
    Serial.print(".");
    Serial.print(uclock.getMonth());
    Serial.print(".");
    Serial.println(uclock.getYear());

    // Set flags and counters for GeoLoc update process
    GeoLocNeedsUpdate = true;
    GeoLocFailedAttempts = 0;
    GeoLocNextRetryMillis = 0;
    GeoLocAttemptDay = currentDay;
  }
}

void processGeoLocUpdate()
{
  if (!GeoLocNeedsUpdate)
  {
    return; // no update needed
  }

  const uint32_t now = millis();
  const uint8_t today = uclock.getDay();

  if (GeoLocAttemptDay != today)
  { // New day, reset attempt counter
    GeoLocFailedAttempts = 0;
    GeoLocNextRetryMillis = 0;
    GeoLocAttemptDay = today;
  }

  if (GeoLocFailedAttempts >= GEOLOC_MAX_FAILURES_PER_DAY)
  {
    Serial.println("GeoLocation update skipped: failure limit reached for today.");
    GeoLocNeedsUpdate = false;
    return; // give up for today
  }

  if (now < GeoLocNextRetryMillis)
  {
    return; // not yet time for next retry
  }

  Serial.println("Daily update for geolocation timezone offset...");

  const int32_t GeoLocTZOffsetOld = uclock.getTimeZoneOffset() / 3600;
  Serial.print("Current TZ offset (hours): ");
  Serial.println(GeoLocTZOffsetOld);

  Serial.println("Querying GeoLocation API...");
  if (GetGeoLocationTimeZoneOffset())
  {
    const int32_t GeoLocTOffsetNew = uclock.getTimeZoneOffset() / 3600;
    Serial.print("New TZ offset (hours): ");
    Serial.println(GeoLocTOffsetNew);

    GeoLocNeedsUpdate = false;
    GeoLocFailedAttempts = 0;
    GeoLocNextRetryMillis = 0;
    yesterday = today;
    return; // success for today!
  }

  GeoLocFailedAttempts++;
  if (GeoLocFailedAttempts >= GEOLOC_MAX_FAILURES_PER_DAY)
  {
    Serial.println("GeoLocation update aborted after repeated failures today.");
    GeoLocNeedsUpdate = false;
    return; // give up for today
  }

  // Schedule next retry
  GeoLocNextRetryMillis = now + GEOLOC_RETRY_BACKOFF_MS;
  Serial.print("GeoLocation retry scheduled in ");
  Serial.print(GEOLOC_RETRY_BACKOFF_MS / 1000);
  Serial.println(" seconds.");
}
#endif // GEOLOCATION_ENABLED

void updateClockDisplay(TFTs::show_t show)
{
  // Refresh, starting with seconds.
  tfts.setDigit(SECONDS_ONES, uclock.getSecondsOnes(), show);
  tfts.setDigit(SECONDS_TENS, uclock.getSecondsTens(), show);
  tfts.setDigit(MINUTES_ONES, uclock.getMinutesOnes(), show);
  tfts.setDigit(MINUTES_TENS, uclock.getMinutesTens(), show);
  tfts.setDigit(HOURS_ONES, uclock.getHoursOnes(), show);
  tfts.setDigit(HOURS_TENS, uclock.getHoursTens(), show);
}
