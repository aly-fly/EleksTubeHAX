#include "SetupPortal.h"

#ifdef WEB_SETUP_PORTAL

/*
 * Small setup portal without external libraries: the clock opens an open WiFi access point,
 * a DNS server answers every name with 192.168.4.1 (captive portal) and a web server sends one
 * gzip compressed page (include/SetupPortalPage.h, ~1 KB). Uses the WebServer and DNSServer
 * libraries of the ESP32 Arduino core.
 *
 *   GET  /      setup page (gzip)
 *   GET  /cfg   current settings as JSON (never the passwords)
 *   GET  /scan  names of the WiFi networks in range as JSON
 *   POST /save  store the settings and restart
 *   GET  /exit  close without changes
 *   other       redirect to the setup page (makes phones show the "sign in to network" page)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <ctype.h>
#include "Backlights.h"
#include "TFTs.h"
#include "SetupPortalPage.h"

extern char UniqueDeviceName[32];

HaxSettings hax;
bool WifiPortalActive = false;

static const char *HAX_NAMESPACE = "hax_setup"; // NVS namespace (max 15 chars)

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
  day_screen_level = 255;
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
  day_screen_level = p.getUChar("dy_scr", day_screen_level);
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
  p.putUChar("dy_scr", day_screen_level);
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
// WiFi credentials (stored by the ESP32 WiFi driver itself)
//----------------------------------------------------------------------------

static String savedWifiSsid()
{
  wifi_config_t conf;
  memset(&conf, 0, sizeof(conf));
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK)
    return String();
  char ssid[sizeof(conf.sta.ssid) + 1]; // the stored SSID is not NUL terminated if it is 32 chars long
  memcpy(ssid, conf.sta.ssid, sizeof(conf.sta.ssid));
  ssid[sizeof(conf.sta.ssid)] = '\0';
  return String(ssid);
}

bool wifiHasSavedCredentials()
{
  return savedWifiSsid().length() > 0;
}

//----------------------------------------------------------------------------
// The setup portal
//----------------------------------------------------------------------------

static void showOnScreens(const char *apName)
{
  tfts.clear(); // selects and enables all displays
  tfts.fillScreen(TFT_BLACK);
#ifdef DISPLAY_SMALL
  tfts.setCursor(0, 0, 1); // 80x160 displays: font 1 (6x8 pixel), long names wrap to the next line
#else
  tfts.setCursor(0, 0, 2); // font 2, 16 pixel high
#endif
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
#ifndef DISPLAY_SMALL
  tfts.println("(opens by itself");
  tfts.println("on most phones)");
#endif
}

static String jsonString(const String &s)
{
  String out = "\"";
  for (size_t i = 0; i < s.length(); i++)
  {
    const char c = s[i];
    if (c == '"' || c == '\\')
    {
      out += '\\';
      out += c;
    }
    else if ((unsigned char)c < 0x20)
      out += ' ';
    else
      out += c;
  }
  return out + "\"";
}

static String htmlText(const String &s)
{
  String out;
  for (size_t i = 0; i < s.length(); i++)
  {
    const char c = s[i];
    if (c == '<')
      out += "&lt;";
    else if (c == '>')
      out += "&gt;";
    else if (c == '&')
      out += "&amp;";
    else
      out += c;
  }
  return out;
}

static void sendMessagePage(WebServer &server, const String &text)
{
  server.send(200, "text/html",
              "<!DOCTYPE html><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
              "<body style=\"font-family:sans-serif;background:#111;color:#eee;padding:16px\"><p>" +
                  text + "</p>");
}

bool runSetupPortal(bool atBoot)
{
  enum
  {
    RUNNING,
    SAVED,
    CLOSED
  } result = RUNNING;
  String newSsid, newPass;
  bool newWifi = false;

  WifiPortalActive = true;
  const char *apName = UniqueDeviceName; // e.g. "ipstube-a1b2c3"
  Serial.printf("\nStarting web setup. Join WiFi \"%s\" and open http://192.168.4.1\n", apName);
  showOnScreens(apName);

  WiFi.disconnect(false, false); // stop connection attempts, keep the stored credentials
  WiFi.mode(WIFI_AP_STA);        // access point + station (the station part is needed for the network scan)
  const IPAddress apIp(192, 168, 4, 1);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName); // open network, only active during the setup

  DNSServer dns;
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", apIp); // every name -> the clock

  WebServer server(80);

  server.on("/", HTTP_GET, [&]()
            {
              server.sendHeader("Content-Encoding", "gzip");
              server.send_P(200, "text/html", (const char *)SETUP_PAGE_GZ, SETUP_PAGE_GZ_LEN); });

  server.on("/cfg", HTTP_GET, [&]()
            {
              String json = "{\"ssid\":" + jsonString(savedWifiSsid());
              json += ",\"host\":" + jsonString(hax.mqtt_host);
              json += ",\"port\":" + String(hax.mqtt_port);
              json += ",\"user\":" + jsonString(hax.mqtt_user);
              json += ",\"strip\":" + String(hax.strip_fitted ? "true" : "false");
#ifdef HARDWARE_IPSTUBE_CLOCK
              json += ",\"stripopt\":true}"; // only the IPSTube can have the front LED strip
#else
              json += ",\"stripopt\":false}";
#endif
              server.send(200, "application/json", json); });

  server.on("/scan", HTTP_GET, [&]()
            {
              const int n = WiFi.scanNetworks(); // blocks ~2-4 s
              String json = "[";
              int listed = 0;
              for (int i = 0; i < n && listed < 30; i++)
              {
                const String ssid = WiFi.SSID(i);
                if (ssid.length() == 0)
                  continue; // hidden network
                bool duplicate = false;
                for (int j = 0; j < i && !duplicate; j++)
                  duplicate = (WiFi.SSID(j) == ssid);
                if (duplicate)
                  continue; // same network name from several access points
                json += (listed++ ? "," : "") + jsonString(ssid);
              }
              WiFi.scanDelete();
              server.send(200, "application/json", json + "]"); });

  server.on("/save", HTTP_POST, [&]()
            {
              newSsid = server.arg("ssid");
              newSsid.trim();
              newPass = server.arg("pass"); // not trimmed: spaces are allowed in WiFi passwords
              newWifi = newSsid.length() > 0 && (newSsid != savedWifiSsid() || newPass.length() > 0);

              copyTrimmed(hax.mqtt_host, sizeof(hax.mqtt_host), server.arg("host").c_str());
              const long port = server.arg("port").toInt();
              if (port >= 1 && port <= 65535)
                hax.mqtt_port = (uint16_t)port;
              copyTrimmed(hax.mqtt_user, sizeof(hax.mqtt_user), server.arg("user").c_str());
              const String mpass = server.arg("mpass");
              if (mpass == "-")
                hax.mqtt_pass[0] = '\0'; // "-" = no password
              else if (mpass.length() > 0)
                copyTrimmed(hax.mqtt_pass, sizeof(hax.mqtt_pass), mpass.c_str()); // empty = keep the current one
#ifdef HARDWARE_IPSTUBE_CLOCK
              hax.strip_fitted = server.hasArg("strip");
#endif
              hax.save();

              sendMessagePage(server, "Saved. The clock restarts and joins <b>" + htmlText(newSsid.length() ? newSsid : savedWifiSsid()) +
                                          "</b>.<br>If it cannot connect: clock menu &gt; Web setup.");
              result = SAVED; });

  server.on("/exit", HTTP_GET, [&]()
            {
              sendMessagePage(server, "Setup closed.");
              result = CLOSED; });

  server.onNotFound([&]()
                    {
                      server.sendHeader("Location", "http://192.168.4.1/");
                      server.send(302, "text/plain", ""); });

  server.begin();

  const uint32_t timeoutMs = (atBoot ? 900UL : 600UL) * 1000UL;
  const uint32_t startMs = millis();
  while (result == RUNNING && (millis() - startMs) < timeoutMs)
  {
    dns.processNextRequest();
    server.handleClient();
    delay(2);
  }

  // Give the browser a moment to receive the last answer.
  const uint32_t flushMs = millis();
  while ((millis() - flushMs) < 500)
  {
    server.handleClient();
    delay(2);
  }
  server.stop();
  dns.stop();
  WifiPortalActive = false;

  if (result == SAVED && newWifi)
  {
    Serial.printf("Setup: storing WiFi \"%s\".\n", newSsid.c_str());
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(true); // let the WiFi driver store the credentials
    WiFi.begin(newSsid.c_str(), newPass.length() ? newPass.c_str() : NULL);
    delay(300);
  }

  if (result != SAVED && atBoot)
  {
    Serial.println("Setup closed or timed out without configuration. Continuing without WiFi.");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    return false;
  }

  Serial.println("Setup finished, restarting the clock...");
  tfts.clear();
  tfts.fillScreen(TFT_BLACK);
  tfts.setCursor(0, 0, 2);
  tfts.setTextColor(TFT_GREEN, TFT_BLACK);
  tfts.println("Restarting...");
  delay(1000);
  ESP.restart();
  return true; // not reached
}

#endif // WEB_SETUP_PORTAL
