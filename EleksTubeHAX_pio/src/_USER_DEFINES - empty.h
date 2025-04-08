/*
 * Project: Alternative firmware for EleksTube IPS clock
 * Hardware: ESP32
 * File description: User preferences for the complete project
 * Hardware connections and other deep settings are located in "GLOBAL_DEFINES.h"
 */

#ifndef USER_DEFINES_H_
#define USER_DEFINES_H_

// #define DEBUG_OUTPUT // uncomment for Debug printing via serial interface

// #define DEBUG_OUTPUT_IMAGES // uncomment for Debug printing of image loading and drawing
// #define DEBUG_OUTPUT_MQTT // uncomment for Debug printing of MQTT messages
// #define DEBUG_OUTPUT_RTC // uncomment for Debug printing of RTC chip initialization and time setting

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

// ************* MQTT plain mode config *************
// #define MQTT_PLAIN_ENABLED                       // enable MQTT support for the external provider

// MQTT support is limited to what an external service offers (for example SmartNest.cz).
// You can use MQTT to control the clock via direct MQTT messages from external service or some DIY device.
// The actual MQTT implementation is "emulating" a temperature sensor, so you can use "set temperature" commands to control the clock from the SmartNest app.

// For plain MQTT support you can either use any internet-based MQTT broker (i.e. smartnest.cz or HiveMQ) or a local one (i.e. Mosquitto).
// If you choose an internet based one, you will need to create an account, (maybe setting up the device there) and filling in the data below then.
// If you choose a local one, you will need to set up the broker on your local network and fill in the data below.

#ifdef MQTT_PLAIN_ENABLED
#define MQTT_BROKER "smartnest.cz"                   // Broker host
#define MQTT_PORT 1883                               // Broker port
#define MQTT_USERNAME "__enter_your_username_here__" // Username from Smartnest
#define MQTT_PASSWORD "__enter_your_api_key_here__"  // Password from Smartnest or API key (under MY Account)
#define MQTT_CLIENT "__enter_your_device_id_here__"  // Device Id from Smartnest
#endif

// ************* MQTT HomeAssistant config *************
// #define MQTT_HOME_ASSISTANT // Uncomment if you want Home Assistant (HA) support

// You will either need a local MQTT broker to use MQTT with Home Assistant (e.g. Mosquitto) or use an internet-based broker with Home Assistant support.
// If not done already, you can set up a local one easily via an Add-On in HA. See: https://www.home-assistant.io/integrations/mqtt/
// Enter the credential data into the MQTT broker settings section below accordingly.
// The device will send auto-discovery messages to Home Assistant via MQTT, so you can use the device in Home Assistant without any custom configuration needed.
// See https://www.home-assistant.io/integrations/mqtt/#discovery-messages-and-availability for more information.
// Retained messages can create ghost entities that keep coming back (if you change MQTT_CLIENT i.e.)! You need to delete them manually from the broker queue!

#ifdef MQTT_HOME_ASSISTANT
#define MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MANUFACTURER "EleksMaker" // Name of the manufacturer shown in HA
#define MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL "Elekstube IPS"     // Name of the model shown in HA
#define MQTT_HOME_ASSISTANT_DISCOVERY_SW_VERSION "1.1"                 // Firmware version shown in HA
#define MQTT_HOME_ASSISTANT_DISCOVERY_HW_VERSION "2.3.04"              // Hardware version shown in HA
#endif

// --- MQTT broker settings ---
// Fill in the data according to configuration of your local MQTT broker that is linked to HomeAssistant - for example Mosquitto.
#ifdef MQTT_HOME_ASSISTANT
#define MQTT_BROKER "_enter_IP_of_the_broker_" // Broker host
#define MQTT_PORT 1883                         // Broker port
#define MQTT_USERNAME "_enter_MQTT_username_"  // Username
#define MQTT_PASSWORD "_enter_MQTT_password_"  // Password
#define MQTT_CLIENT "clock"                    // Device Id
#endif

#define MQTT_SAVE_PREFERENCES_AFTER_SEC 60 // auto save config X seconds after last MQTT configuration message received

// #define MQTT_USE_TLS                                 // Use TLS for MQTT connection. Setting a root CA certificate is needed!
// Don't forget to copy the correct certificate file into the 'data' folder and rename it to mqtt-ca-root.pem!
// Example CA cert (Let's Encrypt CA cert) can be found in the 'data - other graphics' subfolder in the root of this repo

#endif // USER_DEFINES_H_
