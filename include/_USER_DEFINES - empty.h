/*
 * Project: Alternative firmware for EleksTube IPS clock
 * Hardware: ESP32
 * File description: User preferences for the complete project
 * Hardware connections and other deep settings are located in "GLOBAL_DEFINES.h"
 */

#ifndef USER_DEFINES_H_
#define USER_DEFINES_H_

// #define DEBUG_OUTPUT        // Uncomment for Debug printing via serial interface
// #define DEBUG_OUTPUT_IMAGES // Uncomment for Debug printing of image loading and drawing
// #define DEBUG_OUTPUT_MQTT   // Uncomment for Debug printing of MQTT messages
// #define DEBUG_OUTPUT_RTC    // Uncomment for Debug printing of RTC chip initialization and time setting

// ************* Clock font file type selection (.clk or .bmp)  *************
// #define USE_CLK_FILES   // Select between .CLK and .BMP images

// ************* Display Dimming / Night time operation *************
#define DIMMING                      // Uncomment to enable dimming in the given time period between NIGHT_TIME and DAY_TIME
#define NIGHT_TIME 22                // Dim displays at 10 pm
#define DAY_TIME 7                   // Full brightness after 7 am
#define BACKLIGHT_DIMMED_INTENSITY 1 // 0..7
#define TFT_DIMMED_INTENSITY 20      // 0..255

// ************* WiFi config *************
#define WIFI_CONNECT_TIMEOUT_SEC 20
#define WIFI_RETRY_CONNECTION_SEC 15
#define WIFI_USE_WPS                                    // Uncomment to use WPS instead of hard coded wifi credentials
#define WIFI_SSID "__enter_your_wifi_ssid_here__"       // Not needed if WPS is used
#define WIFI_PASSWD "__enter_your_wifi_password_here__" // Not needed if WPS is used. Caution - Hard coded password is stored as clear text in BIN file

//  *************  Geolocation  *************
// Get your API Key on https://www.abstractapi.com/ (login) --> https://app.abstractapi.com/api/ip-geolocation/tester (key) *************
// #define GEOLOCATION_ENABLED // Enable after creating an account and copying Geolocation API below:
#define GEOLOCATION_API_KEY "__enter_your_api_key_here__"

// ************* NTP config  *************
#define NTP_SERVER "pool.ntp.org"
#define NTP_UPDATE_INTERVAL 60000

// ************* MQTT plain mode config *************
// #define MQTT_PLAIN_ENABLED // Enable MQTT support for an external provider

// MQTT support is limited to what an external service offers (for example SmartNest.cz).
// You can use MQTT to control the clock via direct MQTT messages from external service or some DIY device.
// The actual MQTT implementation is "emulating" a temperature sensor, so you can use "set temperature" commands to control the clock from the SmartNest app.

// For plain MQTT support you can either use any internet-based MQTT broker (e.g., smartnest.cz or HiveMQ) or a local one (e.g., Mosquitto).
// If you choose an internet based one, you will need to create an account, (maybe setting up the device there) and filling in the data below then.
// If you choose a local one, you will need to set up the broker on your local network and fill in the data below.

#ifdef MQTT_PLAIN_ENABLED
#define MQTT_BROKER "smartnest.cz"                                       // Broker host
#define MQTT_PORT 1883                                                   // Broker port
#define MQTT_USERNAME "__enter_your_username_here__"                     // Username from Smartnest
#define MQTT_PASSWORD "__enter_your_api_key_here__"                      // Password from Smartnest or API key (under MY Account)
// #define MQTT_CLIENT_ID_FOR_SMARTNEST "__enter_your_device_id_here__"     // Device ID from Smartnest
#endif

// ************* MQTT HomeAssistant config *************
// #define MQTT_HOME_ASSISTANT // Uncomment if you want Home Assistant (HA) support

// You will either need a local MQTT broker to use MQTT with Home Assistant (e.g., Mosquitto) or use an internet-based broker with Home Assistant support.
// If not done already, you can set up a local one easily via an Add-On in HA. See: https://www.home-assistant.io/integrations/mqtt/
// Enter the credential data into the MQTT broker settings section below accordingly.
// The device will send auto-discovery messages to Home Assistant via MQTT, so you can use the device in Home Assistant without any custom configuration needed.
// See https://www.home-assistant.io/integrations/mqtt/#discovery-messages-and-availability for more information.
// Retained messages can create ghost entities that keep coming back (i.e., if you change MQTT device name)! You need to delete them manually from the broker queue!

// Note that the following ACL may need to be set in Mosquitto in order to let the device access and write the necessary topics:
//   user <username>
//   topic read homeassistant/status
//   pattern readwrite homeassistant/+/%c/#
//   pattern readwrite %c/#

// --- MQTT broker settings ---
// Fill in the data according to configuration of your local MQTT broker that is linked to HomeAssistant - for example Mosquitto.
#ifdef MQTT_HOME_ASSISTANT
#define MQTT_BROKER "_enter_IP_of_the_broker_" // Broker host
#define MQTT_PORT 1883                         // Broker port
#define MQTT_USERNAME "_enter_MQTT_username_"  // Username
#define MQTT_PASSWORD "_enter_MQTT_password_"  // Password
#endif

#define MQTT_SAVE_PREFERENCES_AFTER_SEC 60 // auto save config X seconds after last MQTT configuration message received


// Uncomment to append short MAC suffix to device name in Home Assistant for disambiguation when multiple identical models exist
#define ENABLE_HA_DEVICE_NAME_SUFFIX

// #define MQTT_USE_TLS // Use TLS for MQTT connection. Setting a root CA certificate is needed!
// Don't forget to copy the correct certificate file into the 'data' folder and rename it to mqtt-ca-root.pem!
// Example CA cert (Let's Encrypt CA cert) can be found in the 'data - other graphics' subfolder in the root of this repo

#endif // USER_DEFINES_H_
