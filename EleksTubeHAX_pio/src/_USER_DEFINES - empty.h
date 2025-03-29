/*
 * Project: Alternative firmware for EleksTube IPS clock
 * Hardware: ESP32
 * File description: User preferences for the complete project
 * Hardware connections and other deep settings are located in "GLOBAL_DEFINES.h"
 */

#ifndef USER_DEFINES_H_
#define USER_DEFINES_H_

// #define DEBUG_OUTPUT

// ************* Type of the clock hardware  *************
#define HARDWARE_Elekstube_CLOCK // uncomment for the original Elekstube clock
// #define HARDWARE_Elekstube_CLOCK_Gen2 // uncomment for the original Elekstube clock Gen2.1 (ESP32 Pico D4 Chip)
// #define HARDWARE_SI_HAI_CLOCK         // uncomment for the SI HAI copy of the clock
// #define HARDWARE_NovelLife_SE_CLOCK   // uncomment for the NovelLife SE version (Gesture only) - tested and working!; Non-SE version (Buttons only) NOT tested!; Pro version (Buttons and Gesture) NOT tested!
// #define HARDWARE_PunkCyber_CLOCK      // uncomment for the PunkCyber / RGB Glow tube / PCBway clock
// #define HARDWARE_IPSTUBE_CLOCK        // uncomment for the IPSTUBE clock models (H401 and H402)

// ************* Clock font file type selection (.clk or .bmp)  *************
// #define USE_CLK_FILES   // select between .CLK and .BMP images

// ************* Display Dimming / Night time operation *************
#define DIMMING                      // uncomment to enable dimming in the given time period between NIGHT_TIME and DAY_TIME
#define NIGHT_TIME 22                // dim displays at 10 pm
#define DAY_TIME 7                   // full brightness after 7 am
#define BACKLIGHT_DIMMED_INTENSITY 1 // 0..7
#define TFT_DIMMED_INTENSITY 20      // 0..255

// ************* WiFi config *************
#define WIFI_CONNECT_TIMEOUT_SEC 20
#define WIFI_RETRY_CONNECTION_SEC 15
#define WIFI_USE_WPS                                    // uncomment to use WPS instead of hard coded wifi credentials
#define WIFI_SSID "__enter_your_wifi_ssid_here__"       // not needed if WPS is used
#define WIFI_PASSWD "__enter_your_wifi_password_here__" // not needed if WPS is used.  Caution - Hard coded password is stored as clear text in BIN file

//  *************  Geolocation  *************
// Get your API Key on https://www.abstractapi.com/ (login) --> https://app.abstractapi.com/api/ip-geolocation/tester (key) *************
// #define GEOLOCATION_ENABLED // enable after creating an account and copying Geolocation API below:
#define GEOLOCATION_API_KEY "__enter_your_api_key_here__"

// ************* MQTT config *************
// #define MQTT_ENABLED                       // enable general MQTT support
#define MQTT_SAVE_PREFERENCES_AFTER_SEC 60 // auto save config X seconds after last MQTT message received

// --- MQTT Home Assistant settings ---
// You will either need a local MQTT broker to use MQTT with Home Assistant (e.g. Mosquitto) or use an internet-based broker with Home Assistant support.
// If not done already, you can set up a local one easily via an Add-On in HA. See: https://www.home-assistant.io/integrations/mqtt/
// Enter the credential data into the MQTT broker settings section below accordingly.
// It is your choice if you want the MQTT discovery messages sent to Home Assistant being retained or not. By default, they are not retained.
// See https://www.home-assistant.io/integrations/mqtt/#discovery-messages-and-availability for more information.
// Retained messages can create ghost entities that keep coming back (if you change MQTT_CLIENT i.e.)! You need to delete them manually from the broker queue!

// #define MQTT_HOME_ASSISTANT // Uncomment if you want Home Assistant (HA) support (MQTT_ENABLED must be also enabled)
// all following MQTT_HOME_ASSISTANT_* settings require MQTT_HOME_ASSISTANT to be enabled
// #define MQTT_HOME_ASSISTANT_DISCOVERY                                         // Uncomment if you want HA auto-discovery
// #define MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MANUFACTURER "EleksMaker"        // Name of the manufacturer shown in HA
// #define MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL "Elekstube IPS"            // Name of the model shown in HA
// #define MQTT_HOME_ASSISTANT_DISCOVERY_SW_VERSION "1.0 Home Assistant Edition" // Firmware version shown in HA
// #define MQTT_HOME_ASSISTANT_DISCOVERY_HW_VERSION "2.3.04"                     // Hardware version shown in HA
// #define MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES false                   // Set to true if you want to retain the discovery messages in HA (default: false)

// --- MQTT broker settings ---
// NOTE: If Home Assistant is not enabled, the MQTT support is very limited in the moment!
// You can still use MQTT to control the clock, but only via direct sent MQTT messages, sent from a MQTT client like MQTT Explorer or similar.
// The actual pure MQTT implementation is "emulating" a temperature sensor, so you can use "set temperature" commands to control the clock.
// This is a workaround to have a basic MQTT support.
// For pure MQTT support you can either use any internet-based MQTT broker (i.e. smartnest.cz or HiveMQ) or a local one (i.e. Mosquitto).
// If you choose an internet based one, you will need to create an account, (maybe setting up the device there) and filling in the data below then.
// If you choose a local one, you will need to set up the broker on your local network and fill in the data below.
// It is your choice if you want the state/status messages sent to the broker being retained or not. By default, they are not retained.

#define MQTT_BROKER "smartnest.cz"                   // Broker host
#define MQTT_PORT 1883                               // Broker port
#define MQTT_USERNAME "__enter_your_username_here__" // Username from Smartnest
#define MQTT_PASSWORD "__enter_your_api_key_here__"  // Password from Smartnest or API key (under MY Account)
#define MQTT_CLIENT "__enter_your_device_id_here__"  // Device Id from Smartnest
#define MQTT_RETAIN_STATE_MESSAGES false             // Set to true if you want to retain the state/status messages in the MQTT broker (default: false)
// #define MQTT_USE_TLS                                 // Use TLS for MQTT connection. Setting a root CA certificate is needed!
// Don't forget to copy the correct certificate file into the 'data' folder and rename it to mqtt-ca-root.pem!
// Example CA cert (Let's Encrypt CA cert) can be found in the 'data - other graphics' subfolder in the root of this repo

// ************* Optional temperature sensor *************
// #define ONE_WIRE_BUS_PIN 4 // DS18B20 connected to GPIO4; comment this line if sensor is not connected

#endif // USER_DEFINES_H_
