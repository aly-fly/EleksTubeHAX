#include <Arduino.h>
#include <esp_wps.h>
#include <WiFi.h>
#include "IPGeolocation_AO.h"
#include "StoredConfig.h"
#include "TFTs.h"
#include "WiFi_WPS.h"

extern StoredConfig stored_config;
extern char UniqueDeviceName[32];

WifiState_t WifiState = disconnected;

uint32_t TimeOfWifiReconnectAttempt = 0;
double GeoLocTZoffset = 0;

#ifdef WIFI_USE_WPS // WPS code

static esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT(ESP_WPS_MODE); // Init with defaults

// Set alternative WPS config.
// https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/WPS/WPS.ino
void wpsInitConfig()
{
  memset(&wps_config, 0, sizeof(esp_wps_config_t));
  wps_config.wps_type = ESP_WPS_MODE;
  strcpy(wps_config.factory_info.manufacturer, ESP_MANUFACTURER);
  strcpy(wps_config.factory_info.model_number, ESP_MODEL_NUMBER);
  strcpy(wps_config.factory_info.model_name, ESP_MODEL_NAME);
  strcpy(wps_config.factory_info.device_name, UniqueDeviceName);
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
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    WifiState = disconnected;
    Serial.print("WiFi lost connection. Reason: ");
    Serial.println(info.wifi_sta_disconnected.reason);
    WifiReconnect();
    break;
#ifdef WIFI_USE_WPS // WPS code
  case ARDUINO_EVENT_WPS_ER_SUCCESS:
    WifiState = wps_success;
    Serial.println("WPS Successful, stopping WPS and connecting to: " + String(WiFi.SSID()));
    esp_wifi_wps_disable();
    delay(10);
    WiFi.begin();
    break;
  case ARDUINO_EVENT_WPS_ER_FAILED:
    WifiState = wps_failed;
    Serial.println("WPS Failed, retrying");
    esp_wifi_wps_disable();
    wpsInitConfig();
    esp_wifi_wps_enable(&wps_config);
    esp_wifi_wps_start(0);
    break;
  case ARDUINO_EVENT_WPS_ER_TIMEOUT:
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

  WifiState = connected;

  tfts.println("\nConnected! IP:");
  tfts.println(WiFi.localIP());
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  delay(200);
}

void WifiReconnect()
{
  if ((WifiState == disconnected) && ((millis() - TimeOfWifiReconnectAttempt) > WIFI_RETRY_CONNECTION_SEC * 1000))
  {
    Serial.println("Attempting WiFi reconnection...");
    WiFi.reconnect();
    TimeOfWifiReconnectAttempt = millis();
  }
}

#ifdef WIFI_USE_WPS // WPS code
void WiFiStartWps()
{
  memset(&stored_config.config.wifi, 0, sizeof(stored_config.config.wifi)); // erase all settings
  stored_config.config.wifi.password[0] = '\0';                             // empty string as password
  stored_config.config.wifi.WPS_connected = 0x11;                           // invalid = different than 0x55
  Serial.println("");
  Serial.print("Saving config! Triggered from WPS start...");
  stored_config.save();
  Serial.println(" Done.");

  tfts.setTextColor(TFT_GREEN, TFT_BLACK);
  tfts.println("WPS STARTED!");
  tfts.setTextColor(TFT_RED, TFT_BLACK);
  tfts.println("PRESS WPS BUTTON ON THE ROUTER");

  // Disconnect from wifi first if we were connected.
  WiFi.disconnect(true, true);

  WifiState = wps_active;
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_MODE_STA);

  Serial.println("Starting WPS");

  wpsInitConfig();
  esp_wifi_wps_enable(&wps_config);
  esp_wifi_wps_start(0);

  // Loop until connected.
  tfts.setTextColor(TFT_BLUE, TFT_BLACK);
  while (WifiState != connected)
  {
    delay(2000);
    tfts.print(".");
    Serial.print(".");
  }
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(stored_config.config.wifi.ssid, sizeof(stored_config.config.wifi.ssid), "%s", WiFi.SSID().c_str()); // Copy the SSID into the stored configuration safely
  memset(&stored_config.config.wifi.password, 0, sizeof(stored_config.config.wifi.password));                  // Since the password cannot be retrieved from WPS, overwrite it completely
  stored_config.config.wifi.password[0] = '\0';                                                                // ...and set to an empty string
  stored_config.config.wifi.WPS_connected = StoredConfig::valid;                                               // Mark the configuration as valid
  Serial.println();
  Serial.print("Saving config! Triggered from WPS success...");
  stored_config.save();
  Serial.println(" WPS finished.");
}
#endif

bool GetGeoLocationTimeZoneOffset()
{
  Serial.println("\nStarting Geolocation API query...");
  IPGeolocation location(GEOLOCATION_API_KEY, "ABSTRACT");
  IPGeo IPG;
  if (location.updateStatus(&IPG))
  {

    Serial.println(String("Geo Time Zone: ") + String(IPG.tz));
    Serial.println(String("Geo TZ Offset: ") + String(IPG.offset));          // we are interested in this one, type = double
    Serial.println(String("Geo Current Time: ") + String(IPG.current_time)); // currently not used
    GeoLocTZoffset = IPG.offset;
    return true;
  }
  else
  {
    Serial.println("Geolocation failed.");
    return false;
  }
}
