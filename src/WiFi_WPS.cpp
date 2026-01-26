#include <Arduino.h>
#include <esp_wps.h>
#include <WiFi.h>
#include "StoredConfig.h"
#include "TFTs.h"
#include "WiFi_WPS.h"

extern StoredConfig stored_config;
extern char UniqueDeviceName[32];

WifiState_t WifiState = disconnected;

uint32_t TimeOfWifiReconnectAttempt = 0;
uint32_t WifiReconnectIntervalMs = WIFI_RETRY_CONNECTION_SEC * 1000UL;
const uint32_t WifiReconnectIntervalMaxMs = 60000UL;
bool WifiReconnectInProgress = false;
bool WifiWpsActive = false;

#ifdef WIFI_USE_WPS // WPS code

static esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT(ESP_WPS_MODE); // Init with defaults

static void copy_wps_field(char *dest, size_t dest_size, const char *src)
{
  if (dest_size == 0)
    return;
  if (src == nullptr)
  {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, src, dest_size - 1);
  dest[dest_size - 1] = '\0';
}

// Set alternative WPS config.
// https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/WPS/WPS.ino
void wpsInitConfig()
{
  memset(&wps_config, 0, sizeof(esp_wps_config_t));
  wps_config.wps_type = ESP_WPS_MODE;
  copy_wps_field(wps_config.factory_info.manufacturer, sizeof(wps_config.factory_info.manufacturer), ESP_MANUFACTURER);
  copy_wps_field(wps_config.factory_info.model_number, sizeof(wps_config.factory_info.model_number), ESP_MODEL_NUMBER);
  copy_wps_field(wps_config.factory_info.model_name, sizeof(wps_config.factory_info.model_name), ESP_MODEL_NAME);
  copy_wps_field(wps_config.factory_info.device_name, sizeof(wps_config.factory_info.device_name), UniqueDeviceName);
}
#endif

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_START:
    WifiState = disconnected;
    Serial.println("Station Mode Started");
    break;
  case ARDUINO_EVENT_WIFI_STA_CONNECTED: // IP not yet assigned
    Serial.println("Connected to AP: " + String(WiFi.SSID()));
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    Serial.print("Got IP: ");
    Serial.println(WiFi.localIP());
    WifiState = connected;
    WifiReconnectIntervalMs = WIFI_RETRY_CONNECTION_SEC * 1000UL;
    WifiReconnectInProgress = false;
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    WifiState = disconnected;
    if (!WifiWpsActive)
    {
      Serial.print("WiFi lost connection. Reason: ");
      Serial.println(info.wifi_sta_disconnected.reason);
    }
    WifiReconnect();
    break;
#ifdef WIFI_USE_WPS // WPS code
  case ARDUINO_EVENT_WPS_ER_SUCCESS:
    if (!WifiWpsActive)
      break;
    WifiState = wps_success;
    Serial.println("WPS Successful, stopping WPS and connecting to: " + String(WiFi.SSID()));
    esp_wifi_wps_disable();
    delay(10);
    WiFi.begin();
    break;
  case ARDUINO_EVENT_WPS_ER_FAILED:
    if (!WifiWpsActive)
      break;
    WifiState = wps_failed;
    Serial.println("WPS Failed, retrying");
    esp_wifi_wps_disable();
    wpsInitConfig();
    esp_wifi_wps_enable(&wps_config);
    esp_wifi_wps_start(0);
    break;
  case ARDUINO_EVENT_WPS_ER_TIMEOUT:
    if (!WifiWpsActive)
      break;
    Serial.println("WPS Timeout, retrying");
    tfts.setTextColor(TFT_RED, TFT_BLACK);
    tfts.print("/"); // retry
    tfts.setTextColor(TFT_BLUE, TFT_BLACK);
    esp_wifi_wps_disable();
    wpsInitConfig();
    esp_wifi_wps_enable(&wps_config);
    esp_wifi_wps_start(0);
    WifiState = wps_active;
    break;
#endif
  default:
    break;
  }
}

void WifiBegin()
{
  WifiState = disconnected;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false); // we do our own reconnection handling!
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(UniqueDeviceName); // Set the hostname for DHCP

#ifdef WIFI_USE_WPS
  // no data is saved, start WPS imediatelly
  if (stored_config.config.wifi.WPS_connected != StoredConfig::valid)
  {
    // Config is invalid, probably a new device never had its config written.
    Serial.println("Loaded Wifi config is invalid. Not connecting to WiFi.");
    WiFiStartWps(); // infinite loop until connected
  }
  else
  {
    // Data is saved, connect now.
    // WiFi credentials are known, connect.
    tfts.println("Joining WiFi");
    tfts.println(stored_config.config.wifi.ssid);
    Serial.print("Joining WiFi ");
    Serial.println(stored_config.config.wifi.ssid);

    // https://stackoverflow.com/questions/48024780/esp32-wps-reconnect-on-power-on
    WiFi.begin(); // Use internally-saved data
    WiFi.onEvent(WiFiEvent);

    unsigned long StartTime = millis();

    while ((WiFi.status() != WL_CONNECTED))
    {
      delay(500);
      tfts.print(".");
      Serial.print(".");
      if ((millis() - StartTime) > (WIFI_CONNECT_TIMEOUT_SEC * 1000))
      {
        Serial.println("\r\nWiFi connection timeout!");
        tfts.println("\nTIMEOUT!");
        WifiState = disconnected;
        return; // exit loop, exit procedure, continue clock startup
      }
    }
  }
#else // NO WPS -- Try using hardcoded credentials.
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  WiFi.onEvent(WiFiEvent);
  unsigned long StartTime = millis();
  while ((WiFi.status() != WL_CONNECTED))
  {
    delay(500);
    tfts.print(">");
    Serial.print(">");
    if ((millis() - StartTime) > (WIFI_CONNECT_TIMEOUT_SEC * 1000))
    {
      tfts.setTextColor(TFT_RED, TFT_BLACK);
      tfts.println("\nTIMEOUT!");
      tfts.setTextColor(TFT_WHITE, TFT_BLACK);
      Serial.println("\r\nWiFi connection timeout!");
      WifiState = disconnected;
      return; // Exit loop, exit procedure, continue clock startup
    }
  }

#endif

  if (WifiState == connected)
  {
    tfts.println("\nConnected! IP:");
    tfts.println(WiFi.localIP());
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    delay(200);
  }
  else
  {
    Serial.println("Connecting to WiFi failed! No WiFi! Clock will not show actual time or last saved time!");
  }
}

void WifiReconnect()
{
#ifdef WIFI_USE_WPS
  static bool warnedNoCreds = false;
  if (WifiWpsActive)
  {
    return;
  }
  if (stored_config.config.wifi.WPS_connected != StoredConfig::valid)
  {
    if (!warnedNoCreds)
    {
        Serial.println("WiFi reconnect skipped: no stored credentials (WPS not completed).");
      warnedNoCreds = true;
    }
    return;
  }
#endif
  if (WifiReconnectInProgress)
  {
    if ((millis() - TimeOfWifiReconnectAttempt) < 5000UL)
    {
      return;
    }
    WifiReconnectInProgress = false; // allow a new attempt after timeout
  }

  if ((WifiState == disconnected) && ((millis() - TimeOfWifiReconnectAttempt) > WifiReconnectIntervalMs))
  {
    Serial.println("Attempting WiFi reconnection...");
    WifiReconnectInProgress = true;
    WiFi.disconnect(false, false);
    delay(200);
    WiFi.begin();
    TimeOfWifiReconnectAttempt = millis();
    WifiReconnectIntervalMs = min(WifiReconnectIntervalMs * 2, WifiReconnectIntervalMaxMs);
  }
}

#ifdef WIFI_USE_WPS // WPS code
void WiFiStartWps()
{
  const uint32_t WPS_RESTART_INTERVAL_MS = 30000; // Restart WPS every 30s to catch late router activation
  const uint8_t WPS_HARD_RESET_EVERY = 3;         // Full WiFi reset every 3 restarts
  memset(&stored_config.config.wifi, 0, sizeof(stored_config.config.wifi)); // erase all settings
  stored_config.config.wifi.password[0] = '\0';                             // empty string as password
  stored_config.config.wifi.WPS_connected = 0x11;                           // invalid = different than 0x55
  Serial.println("");
  Serial.print("Saving config! Triggered from WPS start (erasing)...");
  stored_config.save();
  Serial.println(" Done.");

  tfts.setTextColor(TFT_GREEN, TFT_BLACK);
  tfts.println("WPS STARTED!");
  tfts.setTextColor(TFT_RED, TFT_BLACK);
  tfts.println("PRESS WPS BUTTON ON THE ROUTER");

  // Disconnect from wifi first if we were connected.
  WiFi.disconnect(true, true);

  WifiState = wps_active;
  WifiWpsActive = true;
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_MODE_STA);

  Serial.println("Starting WPS");

  wpsInitConfig();
  esp_wifi_wps_enable(&wps_config);
  esp_wifi_wps_start(0);
  uint32_t lastWpsStartMs = millis();
  uint32_t wpsStartMs = millis();
  uint8_t wpsRestartCount = 0;

  // Loop until connected.
  tfts.setTextColor(TFT_BLUE, TFT_BLACK);
  while (WifiState != connected)
  {
    delay(2000);
    tfts.print(".");
    Serial.print(".");
    if ((millis() - wpsStartMs) > (WIFI_WPS_CONNECT_TIMEOUT_SEC * 1000UL))
    {
      tfts.setTextColor(TFT_RED, TFT_BLACK);
      tfts.println("WPS FAILED! NO WiFi");
      tfts.setTextColor(TFT_WHITE, TFT_BLACK);
      Serial.println("\r\nWPS FAILED! Going on without WiFi.");
      WifiState = disconnected;
      break;
    }
    if ((millis() - lastWpsStartMs) > WPS_RESTART_INTERVAL_MS)
    {
      wpsRestartCount++;
      Serial.println("\r\nWPS restart to catch late router activation");
      tfts.setTextColor(TFT_ORANGE, TFT_BLACK);
      tfts.print("/");
      tfts.setTextColor(TFT_BLUE, TFT_BLACK);
      if ((wpsRestartCount % WPS_HARD_RESET_EVERY) == 0)
      {
        Serial.println("WPS full WiFi reset");
        tfts.setTextColor(TFT_YELLOW, TFT_BLACK);
        tfts.print("/");
        tfts.setTextColor(TFT_BLUE, TFT_BLACK);
        WiFi.disconnect(true, true);
        delay(200);
        WiFi.mode(WIFI_MODE_STA);
        delay(200);
      }
      esp_wifi_wps_disable();
      wpsInitConfig();
      esp_wifi_wps_enable(&wps_config);
      esp_wifi_wps_start(0);
      WifiState = wps_active;
      lastWpsStartMs = millis();
    }
  }
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  if (WifiState != connected)
  {
    esp_wifi_wps_disable();
    WifiWpsActive = false;
    Serial.println("WPS finished without success! No credentials saved.");
    return;
  }
  snprintf(stored_config.config.wifi.ssid, sizeof(stored_config.config.wifi.ssid), "%s", WiFi.SSID().c_str()); // Copy the SSID into the stored configuration safely
  memset(&stored_config.config.wifi.password, 0, sizeof(stored_config.config.wifi.password));                  // Since the password cannot be retrieved from WPS, overwrite it completely
  stored_config.config.wifi.password[0] = '\0';                                                                // ...and set to an empty string
  stored_config.config.wifi.WPS_connected = StoredConfig::valid;                                               // Mark the configuration as valid
  Serial.println();
  Serial.print("Saving config! Triggered from WPS success (saving)...");
  stored_config.save();
  Serial.println(" WPS finished.");
  WifiWpsActive = false;
}
#endif
