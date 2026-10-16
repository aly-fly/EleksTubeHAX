#include "SetupPortal.h"

#ifdef WEB_SETUP_PORTAL

#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager (tzapu/WiFiManager in platformio.ini)
#include <vector>
#include <ctype.h>
#include "Backlights.h"
#include "TFTs.h"

extern char UniqueDeviceName[32];

HaxSettings hax;
bool WifiPortalActive = false;

static const char *HAX_NAMESPACE = "hax_setup"; // NVS namespace (max 15 chars)

//----------------------------------------------------------------------------
// Small helpers
//----------------------------------------------------------------------------

// Copy src to dst (always NUL terminated), trimming leading/trailing white space.
static void copyTrimmed(char *dst, size_t dstSize, const char *src)
{
  if (dstSize == 0)
    return;
  while (*src != '\0' && isspace((unsigned char)*src))
    src++;
  size_t len = strlen(src);
  while (len > 0 && isspace((unsigned char)src[len - 1]))
    len--;
  if (len >= dstSize)
    len = dstSize - 1;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

static bool looksLikeYes(const char *s)
{
  while (*s != '\0' && isspace((unsigned char)*s))
    s++;
  char c = (char)tolower((unsigned char)*s);
  return (c == 'y' || c == 't' || c == '1'); // yes / true / 1
}

//----------------------------------------------------------------------------
// HaxSettings: persistent runtime settings
//----------------------------------------------------------------------------

void HaxSettings::setDefaults()
{
  mqtt_host[0] = '\0';
  mqtt_port = 1883;
  mqtt_user[0] = '\0';
  mqtt_pass[0] = '\0';

  strip_fitted = false;
  tz_auto = true;

  night_enabled = true;
  night_start_hour = NIGHT_TIME;                // defaults come from _USER_DEFINES.h
  day_start_hour = DAY_TIME;
  night_screen_level = TFT_DIMMED_INTENSITY;
  night_backlight_level = BACKLIGHT_DIMMED_INTENSITY;
}

void HaxSettings::load()
{
  setDefaults();

  Preferences p;
  if (!p.begin(HAX_NAMESPACE, false))
  {
    Serial.println("Setup: could not open settings storage, using defaults.");
    return;
  }

  if (p.isKey("mq_host"))
    p.getString("mq_host", mqtt_host, sizeof(mqtt_host));
  if (p.isKey("mq_user"))
    p.getString("mq_user", mqtt_user, sizeof(mqtt_user));
  if (p.isKey("mq_pass"))
    p.getString("mq_pass", mqtt_pass, sizeof(mqtt_pass));

  uint16_t port = p.getUShort("mq_port", mqtt_port);
  mqtt_port = (port >= 1) ? port : 1883;

  strip_fitted = p.getBool("strip", strip_fitted);
  tz_auto = p.getBool("tz_auto", tz_auto);
  night_enabled = p.getBool("ng_en", night_enabled);
  night_start_hour = p.getUChar("ng_start", night_start_hour) % 24;
  day_start_hour = p.getUChar("ng_end", day_start_hour) % 24;
  night_screen_level = p.getUChar("ng_scr", night_screen_level);
  uint8_t bl = p.getUChar("ng_bl", night_backlight_level);
  night_backlight_level = (bl > 7) ? 7 : bl;
  p.end();

  Serial.printf("Setup: loaded settings. MQTT broker: \"%s\":%u, strip: %s, auto timezone: %s, night dimming: %s (%02u:00-%02u:00)\n",
                mqtt_host, mqtt_port, strip_fitted ? "yes" : "no", tz_auto ? "yes" : "no",
                night_enabled ? "on" : "off", night_start_hour, day_start_hour);
}

void HaxSettings::save()
{
  Preferences p;
  if (!p.begin(HAX_NAMESPACE, false))
  {
    Serial.println("Setup: could not open settings storage, NOT saved!");
    return;
  }
  p.putString("mq_host", mqtt_host);
  p.putUShort("mq_port", mqtt_port);
  p.putString("mq_user", mqtt_user);
  p.putString("mq_pass", mqtt_pass);
  p.putBool("strip", strip_fitted);
  p.putBool("tz_auto", tz_auto);
  p.putBool("ng_en", night_enabled);
  p.putUChar("ng_start", night_start_hour);
  p.putUChar("ng_end", day_start_hour);
  p.putUChar("ng_scr", night_screen_level);
  p.putUChar("ng_bl", night_backlight_level);
  p.end();
  dirty = false;
  Serial.println("Setup: settings saved.");
}

void HaxSettings::markDirty()
{
  dirty = true;
  dirtyAtMs = millis();
}

void HaxSettings::loop()
{
  if (dirty && (millis() - dirtyAtMs) > 5000UL)
  {
    save();
  }
}

//----------------------------------------------------------------------------
// Front LED strip
//----------------------------------------------------------------------------

void applyStripLength(bool fitted)
{
  const uint16_t wanted = fitted ? HAX_STRIP_LEDS : HAX_NO_STRIP_LEDS;
  if (backlights.numPixels() == wanted)
    return;

  backlights.clear(); // switch everything off while the old length is still known to the driver
  backlights.show();
  backlights.updateLength(wanted);
  backlights.setPattern(backlights.getPattern()); // forces the current pattern to be re-initialised
  Serial.printf("Backlight length is now %u LEDs.\n", wanted);
}

//----------------------------------------------------------------------------
// WiFi credentials stored?
//----------------------------------------------------------------------------

bool wifiHasSavedCredentials()
{
  wifi_config_t conf;
  memset(&conf, 0, sizeof(conf));
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK)
    return false;
  return conf.sta.ssid[0] != '\0';
}

//----------------------------------------------------------------------------
// The setup portal
//----------------------------------------------------------------------------

static void showPortalInstructionsOnScreens(const char *apName)
{
  tfts.clear(); // selects and enables all displays
  tfts.fillScreen(TFT_BLACK);
  tfts.setCursor(0, 0, 2); // font 2, 16 pixel high

  tfts.setTextColor(TFT_GREEN, TFT_BLACK);
  tfts.println("WEB SETUP");
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.println("1 Join WiFi:");
  tfts.setTextColor(TFT_YELLOW, TFT_BLACK);
  tfts.println(apName);
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.println("2 Open page:");
  tfts.setTextColor(TFT_YELLOW, TFT_BLACK);
  tfts.println("192.168.4.1");
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.println("(opens by itself");
  tfts.println("on most phones)");
}

bool runSetupPortal(bool atBoot)
{
  WifiPortalActive = true;

  const char *apName = UniqueDeviceName; // e.g. "ipstube-a1b2c3"
  Serial.printf("\nStarting web setup portal. Join WiFi \"%s\" and open http://192.168.4.1\n", apName);
  showPortalInstructionsOnScreens(apName);

  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setTitle("IPSTube setup");
  wm.setConfigPortalTimeout(atBoot ? 900 : 600); // seconds
  wm.setConnectTimeout(30);

  char portText[8];
  snprintf(portText, sizeof(portText), "%u", hax.mqtt_port);

  // The text boxes of the setup page.
  WiFiManagerParameter pHost("mq_host", "Home Assistant MQTT broker (IP address or name)", hax.mqtt_host, sizeof(hax.mqtt_host) - 1);
  WiFiManagerParameter pPort("mq_port", "MQTT port", portText, 5, "type=\"number\" min=\"1\" max=\"65535\"");
  WiFiManagerParameter pUser("mq_user", "MQTT username", hax.mqtt_user, sizeof(hax.mqtt_user) - 1);
  WiFiManagerParameter pPass("mq_pass", "MQTT password (empty = keep current, - = none)", "", sizeof(hax.mqtt_pass) - 1, "type=\"password\"");
  WiFiManagerParameter pStrip("strip", "LED strip fitted along the front/bottom? (yes / no)", hax.strip_fitted ? "yes" : "no", 3);
  wm.addParameter(&pHost);
  wm.addParameter(&pPort);
  wm.addParameter(&pUser);
  wm.addParameter(&pPass);
  wm.addParameter(&pStrip);

  // Copies the text boxes into the settings. Returns true if anything differs from before.
  auto readParams = [&]() -> bool
  {
    const HaxSettings before = hax;

    copyTrimmed(hax.mqtt_host, sizeof(hax.mqtt_host), pHost.getValue());
    int port = atoi(pPort.getValue());
    if (port >= 1 && port <= 65535)
      hax.mqtt_port = (uint16_t)port;
    copyTrimmed(hax.mqtt_user, sizeof(hax.mqtt_user), pUser.getValue());

    char pw[sizeof(hax.mqtt_pass)];
    copyTrimmed(pw, sizeof(pw), pPass.getValue());
    if (strcmp(pw, "-") == 0)
      hax.mqtt_pass[0] = '\0'; // "-" means: no password
    else if (pw[0] != '\0')
      memcpy(hax.mqtt_pass, pw, sizeof(pw)); // empty = keep the current password

    hax.strip_fitted = looksLikeYes(pStrip.getValue());

    return strcmp(before.mqtt_host, hax.mqtt_host) != 0 ||
           before.mqtt_port != hax.mqtt_port ||
           strcmp(before.mqtt_user, hax.mqtt_user) != 0 ||
           strcmp(before.mqtt_pass, hax.mqtt_pass) != 0 ||
           before.strip_fitted != hax.strip_fitted;
  };

  bool paramsSaved = false;
  wm.setSaveParamsCallback([&]()
                           {
                             readParams();
                             hax.save();
                             paramsSaved = true;
                             Serial.println("Setup: settings received from the web page."); });

  // At boot there is no WiFi yet: one page with WiFi + all settings. Later (from the menu) WiFi already works:
  // offer the settings page first, the WiFi page is still reachable.
  std::vector<const char *> menu;
  if (atBoot)
  {
    menu = {"wifi", "exit"};
  }
  else
  {
    menu = {"param", "wifi", "exit"}; // "param" puts the settings on their own page
  }
  wm.setMenu(menu);

  bool connected = wm.startConfigPortal(apName); // blocks until finished, timed out or "Exit" was pressed

  if (readParams()) // safety net in case the save callback did not fire: pick up changed values
  {
    hax.save();
    paramsSaved = true;
  }
  WifiPortalActive = false;

  if (connected || paramsSaved)
  {
    Serial.println("Setup finished, restarting the clock...");
  }
  else if (!atBoot)
  {
    Serial.println("Setup closed without changes, restarting the clock...");
  }
  else
  {
    Serial.println("Setup timed out without any configuration. Continuing without WiFi.");
    return false;
  }

  tfts.clear();
  tfts.fillScreen(TFT_BLACK);
  tfts.setCursor(0, 0, 2);
  tfts.setTextColor(TFT_GREEN, TFT_BLACK);
  tfts.println("Restarting...");
  delay(1500);
  ESP.restart();
  return true; // not reached
}

#endif // WEB_SETUP_PORTAL
