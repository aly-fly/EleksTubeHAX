/*
 * Author: Aljaz Ogrin
 * Project: Alternative firmware for EleksTube IPS clock
 * Original location: https://github.com/aly-fly/EleksTubeHAX
 * Hardware: ESP32
 * File description: Supports two operational modes:
 *   1. Simple MQTT messages to an external MQTT server, for example SmartNest
 *   2. Json + Discovery messages for integration into HomeAssistant
 * Optionally supports TLS security.
 *
 * Mode 1:
 * Connects to the MQTT Broker "smartnest.cz". Uses a device "Thermostat".
 * Sends status and receives commands from WebApp, Android app or connected devices (SmartThings, Google assistant, Alexa, etc.)
 * Configuration: open file "GLOBAL_DEFINES.h"
 * Reference: https://github.com/aososam/Smartnest/tree/master/Devices/thermostat
 * Documentation: https://www.docu.smartnest.cz/
 *
 * Mode 2:
 * Connects to the MQTT broker that is connected to HomeAssistant.
 * Clock automatically sends Discovery messages which automatically add and configure a new device in the HA.
 */

#include "MQTT_client_ips.h"

#if defined(MQTT_PLAIN_ENABLED) && defined(MQTT_HOME_ASSISTANT)
#error "Both MQTT modes can't be enabled at the same time!"
#endif

#if defined(MQTT_PLAIN_ENABLED) || defined(MQTT_HOME_ASSISTANT)
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include "Backlights.h"
#include "Clock.h"
#include "TFTs.h"
#ifdef MQTT_USE_TLS // For secure WiFi client
#include <WiFiClientSecure.h>

WiFiClientSecure espClient;
#else
WiFiClient espClient;
#endif

extern char UniqueDeviceName[32];
extern char UniqueDeviceNameLower[32];

// Initialize the MQTT client.
PubSubClient MQTTclient(espClient);

// Functions for general MQTT handling.
void MQTTCallback(char *topic, byte *payload, unsigned int length);
void checkIfMQTTIsConnected();
bool MQTTPublish(const char *Topic, const char *Message, const bool Retain);
bool MQTTPublish(const char *Topic, JsonDocument *Json, const bool Retain);
void MQTTReportState(bool forceUpdateEverything);
void MQTTReportBackOnChange();
void MQTTReportBackEverything(bool forceUpdateEverything);
void MQTTPeriodicReportBack();

// Plain MQTT mode functions.
void MQTTReportPowerState(bool forceUpdate);
void MQTTReportWiFiSignal();
void MQTTReportStatus(bool forceUpdate);

// Home Assistant mode functions.
bool MQTTReportDiscovery();
bool MQTTReportAvailability(const char *status);

// Helper functions.
double round1(double value);
bool endsWith(const char *str, const char *suffix);
#ifdef MQTT_USE_TLS
bool loadCARootCert();
#endif

#define concat7_into(buf, first, second, third, fourth, fifth, sixth, seventh) (snprintf((buf), sizeof(buf), "%s%s%s%s%s%s%s", (first), (second), (third), (fourth), (fifth), (sixth), (seventh)), (buf))

#define MQTT_ROOT_TOPIC "elekstubehax"

// Variables
char outbuf[64];

uint32_t lastTimeSent = (uint32_t)(MQTT_REPORT_STATUS_EVERY_SEC * -1000);
uint32_t LastTimeTriedToConnect = 0;

bool MQTTConnected = false;     // Show connection status on the clock's LCD
bool discoveryReported = false; // Initial state of discovery messages sent to HA
bool availabilityReported = false;

// Commands from server for plain MQTT mode.
int MQTTCommandState = 1;
bool MQTTCommandStateReceived = false;

#ifdef MQTT_HOME_ASSISTANT
// MQTT topics for HA.
#define TopicFront "main"
#define TopicBack "back"
#define Topic12hr "use_twelve_hours"
#define TopicBlank0 "blank_zero_hours"
#define TopicPulse "pulse_bpm"
#define TopicBreath "breath_bpm"
#define TopicRainbow "rainbow_duration"
#endif

bool MQTTCommandMainPower = true;
bool MQTTCommandMainPowerReceived = false;
bool MQTTCommandBackPower = true;
bool MQTTCommandBackPowerReceived = false;
bool MQTTCommandUseTwelveHours = false;
bool MQTTCommandUseTwelveHoursReceived = false;
bool MQTTCommandBlankZeroHours = false;
bool MQTTCommandBlankZeroHoursReceived = false;

uint8_t MQTTCommandBrightness = -1;
bool MQTTCommandBrightnessReceived = false;
uint8_t MQTTCommandMainBrightness = -1;
bool MQTTCommandMainBrightnessReceived = false;
uint8_t MQTTCommandBackBrightness = -1;
bool MQTTCommandBackBrightnessReceived = false;

char MQTTCommandPattern[24] = "";
bool MQTTCommandPatternReceived = false;
char MQTTCommandBackPattern[24] = "";
bool MQTTCommandBackPatternReceived = false;

uint16_t MQTTCommandBackColorPhase = -1;
bool MQTTCommandBackColorPhaseReceived = false;

uint8_t MQTTCommandGraphic = -1;
bool MQTTCommandGraphicReceived = false;
uint8_t MQTTCommandMainGraphic = -1;
bool MQTTCommandMainGraphicReceived = false;

uint8_t MQTTCommandPulseBpm = -1;
bool MQTTCommandPulseBpmReceived = false;

uint8_t MQTTCommandBreathBpm = -1;
bool MQTTCommandBreathBpmReceived = false;

float MQTTCommandRainbowSec = -1;
bool MQTTCommandRainbowSecReceived = false;

// Status to server for Home Assistant.
bool MQTTStatusMainPower = true;
bool MQTTStatusBackPower = true;
bool MQTTStatusUseTwelveHours = true;
bool MQTTStatusBlankZeroHours = true;

// Status to server.
int MQTTStatusState = 0;

uint8_t MQTTStatusBrightness = 0;
uint8_t MQTTStatusMainBrightness = 0;
uint8_t MQTTStatusBackBrightness = 0;
char MQTTStatusPattern[24] = "";
char MQTTStatusBackPattern[24] = "";
uint16_t MQTTStatusBackColorPhase = 0;
uint8_t MQTTStatusGraphic = 0;
uint8_t MQTTStatusMainGraphic = 0;
uint8_t MQTTStatusPulseBpm = 0;
uint8_t MQTTStatusBreathBpm = 0;
float MQTTStatusRainbowSec = 0;

int LastSentMainPowerState = -1;
int LastSentBackPowerState = -1;
int LastSentBrightness = -1;
int LastSentMainBrightness = -1;
int LastSentBackBrightness = -1;
char LastSentPattern[24] = "";
char LastSentBackPattern[24] = "";
int LastSentBackColorPhase = -1;
int LastSentGraphic = -1;
int LastSentMainGraphic = -1;
bool LastSentUseTwelveHours = false;
bool LastSentBlankZeroHours = false;
uint8_t LastSentPulseBpm = -1;
uint8_t LastSentBreathBpm = -1;
float LastSentRainbowSec = -1;

// Plain MQTT.
int LastSentSignalLevel = 999;
int LastSentStatus = -1;

void printMQTTconnectionStatus(void)
{
  switch (MQTTclient.state())
  {
  case MQTT_CONNECTION_TIMEOUT:
    Serial.println("Error: MQTT_CONNECTION_TIMEOUT");
    break;

  case MQTT_CONNECTION_LOST:
    Serial.println("Error: MQTT_CONNECTION_LOST");
    break;

  case MQTT_CONNECT_FAILED:
    Serial.println("Error: MQTT_CONNECT_FAILED");
    break;

  case MQTT_DISCONNECTED:
    Serial.println("Error: MQTT_DISCONNECTED");
    break;

  case MQTT_CONNECT_BAD_PROTOCOL:
    Serial.println("Error: MQTT_CONNECT_BAD_PROTOCOL");
    break;

  case MQTT_CONNECT_BAD_CLIENT_ID:
    Serial.println("Error: MQTT_CONNECT_BAD_CLIENT_ID");
    break;

  case MQTT_CONNECT_UNAVAILABLE:
    Serial.println("Error: MQTT_CONNECT_UNAVAILABLE");
    break;

  case MQTT_CONNECT_BAD_CREDENTIALS:
    Serial.println("Error: MQTT_CONNECT_BAD_CREDENTIALS");
    break;

  case MQTT_CONNECT_UNAUTHORIZED:
    Serial.println("Error: MQTT_CONNECT_UNAUTHORIZED");
    break;

  case MQTT_CONNECTED:
    Serial.println("MQTT_CONNECTED");
    break;

  default:
    Serial.printf("Unknown MQTT error: %d\r\n", MQTTclient.state());
    break;
  }
}

bool MQTTPublish(const char *Topic, const char *Message, const bool Retain)
{
  if (!MQTTclient.connected())
    return false;

  bool ok = MQTTclient.publish(Topic, Message, Retain);

#ifdef DEBUG_OUTPUT_MQTT
  if (ok)
  {
    Serial.print("DEBUG: TX MQTT: Topic: ");
    Serial.print(Topic);
    Serial.print(" - Message: ");
    Serial.print(Message);
    Serial.print(" - Retain: ");
    Serial.println(Retain ? "true" : "false");
  }
  else
  {
    Serial.print("DEBUG: TX MQTT Error for topic: ");
    Serial.println(Topic);
  }
#endif
  return ok;
}

bool MQTTPublish(const char *Topic, JsonDocument *Json, const bool Retain)
{
  size_t buffSize = measureJson(*Json) + 3; // Discovery Light = about 720 bytes
#ifdef DEBUG_OUTPUT_MQTT
  Serial.printf("DEBUG: TX MQTT message JSON size: %d\n", buffSize);
#endif
  char *buffer = (char *)malloc(buffSize);
  if (buffer == NULL)
  {
    Serial.printf("ERROR: Error allocating %d bytes to serialize JSON.\n", buffSize);
    return false;
  }
  size_t dataSize = serializeJson(*Json, buffer, buffSize);
  if ((dataSize < buffSize) && (dataSize > 0))
  {
    bool ok = MQTTPublish(Topic, buffer, Retain);
    Json->clear();
    free(buffer);
    return ok;
  }
  else
  {
    Serial.println("ERROR: Error serializing JSON data.");
    Json->clear();
    free(buffer);
    return false;
  }
}

void MQTTReportState(bool forceUpdateEverything)
{
#ifdef MQTT_HOME_ASSISTANT
  if (!MQTTclient.connected())
  {
    return;
  }
  // Send availability message.
  if (forceUpdateEverything || !availabilityReported)
  {
    if (!MQTTReportAvailability(MQTT_ALIVE_MSG_ONLINE))
    {
      return;
    }
    availabilityReported = true;
  }

  if (forceUpdateEverything || MQTTStatusMainPower != LastSentMainPowerState || MQTTStatusMainBrightness != LastSentMainBrightness || MQTTStatusMainGraphic != LastSentMainGraphic)
  {
    JsonDocument state;
    state["state"] = MQTTStatusMainPower == 0 ? MQTT_STATE_OFF : MQTT_STATE_ON;
    state["brightness"] = MQTTStatusMainBrightness;
    state["effect"] = tfts.clockFaceToName(MQTTStatusMainGraphic);
    state["color_mode"] = "brightness";

    if (MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicFront, "", ""), &state, MQTT_RETAIN_STATE_MESSAGES))
    {
      LastSentMainPowerState = MQTTStatusMainPower;
      LastSentMainBrightness = MQTTStatusMainBrightness;
      LastSentMainGraphic = MQTTStatusMainGraphic;
    }
  }

  if (forceUpdateEverything || MQTTStatusBackPower != LastSentBackPowerState || MQTTStatusBackBrightness != LastSentBackBrightness || strcmp(MQTTStatusBackPattern, LastSentBackPattern) != 0 || MQTTStatusBackColorPhase != LastSentBackColorPhase || MQTTStatusPulseBpm != LastSentPulseBpm || MQTTStatusBreathBpm != LastSentBreathBpm || MQTTStatusRainbowSec != LastSentRainbowSec)
  {
    JsonDocument state;
    state["state"] = MQTTStatusBackPower == 0 ? MQTT_STATE_OFF : MQTT_STATE_ON;
    state["brightness"] = MQTTStatusBackBrightness;
    state["effect"] = MQTTStatusBackPattern;
    state["color_mode"] = "hs";
    state["color"]["h"] = backlights.phaseToHue(MQTTStatusBackColorPhase);
    state["color"]["s"] = 100.f;
    state["pulse_bpm"] = MQTTStatusPulseBpm;
    state["beath_bpm"] = MQTTStatusBreathBpm;
    state["rainbow_sec"] = round1(MQTTStatusRainbowSec);

    if (MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBack, "", ""), &state, MQTT_RETAIN_STATE_MESSAGES))
    {
      LastSentBackPowerState = MQTTStatusBackPower;
      LastSentBackBrightness = MQTTStatusBackBrightness;
      strncpy(LastSentBackPattern, MQTTStatusBackPattern, sizeof(LastSentBackPattern) - 1);
      LastSentBackPattern[sizeof(LastSentBackPattern) - 1] = '\0';
      LastSentBackColorPhase = MQTTStatusBackColorPhase;
    }
  }

  if (forceUpdateEverything || MQTTStatusUseTwelveHours != LastSentUseTwelveHours)
  {
    JsonDocument state;
    state["state"] = MQTTStatusUseTwelveHours ? MQTT_STATE_ON : MQTT_STATE_OFF;

    if (MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", Topic12hr, "", ""), &state, MQTT_RETAIN_STATE_MESSAGES))
    {
      LastSentUseTwelveHours = MQTTStatusUseTwelveHours;
    }
  }

  if (forceUpdateEverything || MQTTStatusBlankZeroHours != LastSentBlankZeroHours)
  {
    JsonDocument state;
    state["state"] = MQTTStatusBlankZeroHours ? MQTT_STATE_ON : MQTT_STATE_OFF;

    if (MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBlank0, "", ""), &state, MQTT_RETAIN_STATE_MESSAGES))
    {
      LastSentBlankZeroHours = MQTTStatusBlankZeroHours;
    }
  }

  if (forceUpdateEverything || MQTTStatusPulseBpm != LastSentPulseBpm)
  {
    JsonDocument state;
    state["state"] = MQTTStatusPulseBpm;

    if (MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicPulse, "", ""), &state, MQTT_RETAIN_STATE_MESSAGES))
    {
      LastSentPulseBpm = MQTTStatusPulseBpm;
    }
  }

  if (forceUpdateEverything || MQTTStatusBreathBpm != LastSentBreathBpm)
  {
    JsonDocument state;
    state["state"] = MQTTStatusBreathBpm;

    if (MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBreath, "", ""), &state, MQTT_RETAIN_STATE_MESSAGES))
    {
      LastSentBreathBpm = MQTTStatusBreathBpm;
    }
  }

  if (forceUpdateEverything || MQTTStatusRainbowSec != LastSentRainbowSec)
  {

    JsonDocument state;
    state["state"] = round1(MQTTStatusRainbowSec);

    if (MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicRainbow, "", ""), &state, MQTT_RETAIN_STATE_MESSAGES))
    {
      LastSentRainbowSec = MQTTStatusRainbowSec;
    }
  }
#endif
}

#ifdef MQTT_USE_TLS
bool loadCARootCert()
{
  const char *filename = "/mqtt-ca-root.pem";
  Serial.println("Loading CA Root Certificate");

  // Check if the PEM file exists.
  if (!SPIFFS.exists(filename))
  {
    Serial.println("ERROR: File not found mqtt-ca-root.pem");
    return false;
  }

  // Open the PEM file in read mode.
  File file = SPIFFS.open(filename, "r");
  if (!file)
  {
    Serial.println("ERROR: Failed to open mqtt-ca-root.pem");
    return false;
  }

  // Get the size of the file.
  size_t size = file.size();
  if (size == 0)
  {
    Serial.println("ERROR: Empty mqtt-ca-root.pem");
    file.close();
    return false;
  }

  // Use the loadCA() method to load the certificate directly from the file stream.
  bool result = espClient.loadCACert(file, size);

  file.close();

  if (result)
  {
    Serial.println("CA Root Certificate loaded successfully");
  }
  else
  {
    Serial.println("ERROR: Failed to load CA Root Certificate");
  }

  return result;
}
#endif

bool MQTTStart(bool restart)
{
  MQTTConnected = false;
  if (((millis() - LastTimeTriedToConnect) > (MQTT_RECONNECT_WAIT_SEC * 1000)) || (LastTimeTriedToConnect == 0))
  { // Try to connect to MQTT broker only if the time since last connection attempt is greater than the defined wait time (default 30 sec).
    if (restart)
    {
      Serial.println("MQTT reconnecting...");
      printMQTTconnectionStatus();
    }
    else
    {
      LastTimeTriedToConnect = millis();
#ifdef DEBUG_OUTPUT_MQTT
      Serial.println("DEBUG: Set MQTT broker to: ");
      Serial.print(MQTT_BROKER);
      Serial.print(":");
      Serial.println(MQTT_PORT);
#endif
      MQTTclient.setServer(MQTT_BROKER, MQTT_PORT);
      MQTTclient.setCallback(MQTTCallback);
      MQTTclient.setBufferSize(2048);
#ifdef MQTT_USE_TLS
      bool result = loadCARootCert();
      if (!result)
      {
        return false; // load certificate failed -> do not continue
      }
#endif
    }
    Serial.println("Connecting to MQTT...");
    // Attempt to connect. Set the last will (LWT) message if the connection get lost.
    if (MQTTclient.connect(UniqueDeviceName, // MQTT client id
                           MQTT_USERNAME,    // MQTT username
                           MQTT_PASSWORD     // MQTT password
#ifndef MQTT_CLIENT_ID_FOR_SMARTNEST
                           ,
                           concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", ""), // Last will topic (rooted for HA/plain unified mode) because smartnest.cz broker does not interpret this
                           0,                                                                                                // Last will QoS
                           MQTT_RETAIN_ALIVE_MESSAGES,                                                                       // Retain message
                           MQTT_ALIVE_MSG_OFFLINE                                                                            // Last will message
#endif
                           ))
    {
      Serial.println("MQTT connected");
      MQTTConnected = true;
      MQTTReportAvailability(MQTT_ALIVE_MSG_ONLINE); // Publish online status
    }
    else
    {
      Serial.println("MQTT connection failed!");
      LastTimeTriedToConnect = millis();
      printMQTTconnectionStatus();
      return false; // Do not continue if not connected
    } // Connect failed

#ifdef DEBUG_OUTPUT_MQTT
    Serial.println("DEBUG: subscribing to MQTT topics...");
#endif

#ifdef MQTT_PLAIN_ENABLED
    bool ok = MQTTclient.subscribe(concat7_into(outbuf, UniqueDeviceNameLower, "/directive/#", "", "", "", "", "")); // Subscribes only to messages send to the device
    if (!ok)
      Serial.println("Error subscribing to /directive messages!");
#ifdef DEBUG_OUTPUT_MQTT
    Serial.println("DEBUG: Subscribed to /directive/# messages sent to the device.");
    Serial.println("DEBUG: Sending initial status messages...");
#endif
    // Send initial status messages.
    MQTTReportAvailability(MQTT_ALIVE_MSG_ONLINE);                                                                                                                     // Reports that the device is online
    MQTTPublish(concat7_into(outbuf, UniqueDeviceNameLower, "/report/firmware", "", "", "", "", ""), FIRMWARE_VERSION, MQTT_RETAIN_STATE_MESSAGES);                    // Reports the firmware version
    MQTTPublish(concat7_into(outbuf, UniqueDeviceNameLower, "/report/ip", "", "", "", "", ""), (char *)WiFi.localIP().toString().c_str(), MQTT_RETAIN_STATE_MESSAGES); // Reports the ip
    MQTTPublish(concat7_into(outbuf, UniqueDeviceNameLower, "/report/network", "", "", "", "", ""), (char *)WiFi.SSID().c_str(), MQTT_RETAIN_STATE_MESSAGES);          // Reports the network name
    MQTTReportWiFiSignal();
#endif // MQTT_PLAIN_ENABLED

#ifdef MQTT_HOME_ASSISTANT
    MQTTclient.subscribe(MQTT_TOPIC_HASTATUS); // Subscribe to homeassistant/status for receiving LWT and Birth messages from Home Assistant
    MQTTclient.subscribe(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicFront, "/set", ""));
    MQTTclient.subscribe(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBack, "/set", ""));
    MQTTclient.subscribe(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", Topic12hr, "/set", ""));
    MQTTclient.subscribe(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBlank0, "/set", ""));
    MQTTclient.subscribe(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBreath, "/set", ""));
    MQTTclient.subscribe(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicPulse, "/set", ""));
    MQTTclient.subscribe(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicRainbow, "/set", ""));
#ifdef DEBUG_OUTPUT_MQTT
    Serial.println("DEBUG: subscribed to topics: ");
    Serial.printf("%s/%s/%s/set\n", MQTT_ROOT_TOPIC, UniqueDeviceNameLower, TopicFront);
    Serial.printf("%s/%s/%s/set\n", MQTT_ROOT_TOPIC, UniqueDeviceNameLower, TopicBack);
    Serial.printf("%s/%s/%s/set\n", MQTT_ROOT_TOPIC, UniqueDeviceNameLower, Topic12hr);
    Serial.printf("%s/%s/%s/set\n", MQTT_ROOT_TOPIC, UniqueDeviceNameLower, TopicBlank0);
    Serial.printf("%s/%s/%s/set\n", MQTT_ROOT_TOPIC, UniqueDeviceNameLower, TopicBreath);
    Serial.printf("%s/%s/%s/set\n", MQTT_ROOT_TOPIC, UniqueDeviceNameLower, TopicPulse);
    Serial.printf("%s/%s/%s/set\n", MQTT_ROOT_TOPIC, UniqueDeviceNameLower, TopicRainbow);
    Serial.println(MQTT_TOPIC_HASTATUS);
#endif // DEBUG_OUTPUT_MQTT
#endif // MQTT_HOME_ASSISTANT
  }
  return true;
}

void checkIfMQTTIsConnected()
{
  MQTTConnected = MQTTclient.connected();
  if (!MQTTConnected)
  {
    availabilityReported = false;
    MQTTStart(true); // Try to reconnect to MQTT broker
  }
}

void MQTTCallback(char *topic, byte *payload, unsigned int length)
{
#ifdef DEBUG_OUTPUT_MQTT
  Serial.println("");
  Serial.println("DEBUG: Entering MQTTCallback...");
  Serial.print("DEBUG: Received topic: ");
  Serial.println(topic);
  Serial.print("DEBUG: Payload length: ");
  Serial.println(length);
#endif

  const size_t bufferSize = 256; // Use a fixed-size character buffer for the payload (adjust size as needed)
  char message[bufferSize];
  memset(message, 0, bufferSize);
  if (length < bufferSize)
  {
    memcpy(message, payload, length);
    message[length] = '\0';
  }
  else
  {
    // In case the incoming payload exceeds our buffer size, truncate it.
    memcpy(message, payload, bufferSize - 1);
    message[bufferSize - 1] = '\0';
    Serial.println("WARNING: MQTT Payload too long, truncated!");
  }

#ifdef DEBUG_OUTPUT_MQTT
  Serial.print("DEBUG: Converted payload to char array: ");
  Serial.println(message);
  Serial.println("DEBUG: Processing MQTT message...");
  Serial.print("DEBUG: RX MQTT: ");
  Serial.print(topic);
  Serial.print(" ");
  Serial.println(message);
#endif

#ifdef MQTT_PLAIN_ENABLED
  // Check if topic ends with "/directive/powerState".
  if (endsWith(topic, "/directive/powerState"))
  {
    // Turn On or OFF based on payload.
    if (strcmp(message, "ON") == 0)
    {
      MQTTCommandMainPower = true;
      MQTTCommandBackPower = true;
      MQTTCommandMainPowerReceived = true;
      MQTTCommandBackPowerReceived = true;
    }
    else if (strcmp(message, "OFF") == 0)
    {
      MQTTCommandMainPower = false;
      MQTTCommandBackPower = false;
      MQTTCommandMainPowerReceived = true;
      MQTTCommandBackPowerReceived = true;
    }
  }
  else if (endsWith(topic, "/directive/setpoint") || endsWith(topic, "/directive/percentage"))
  {
    double valueD = atof(message);
    if (!isnan(valueD))
    {
      MQTTCommandState = (int)valueD;
      MQTTCommandStateReceived = true;
    }
  }
#endif // MQTT_PLAIN_ENABLED

#ifdef MQTT_HOME_ASSISTANT
  if (strcmp(topic, MQTT_TOPIC_HASTATUS) == 0) // Process "homeassistant/status" messages -> react if Home Assistant is online or offline

  {
    if (strcmp(message, "online") == 0)
    {
      uint16_t randomDelay = random(100, 400);
      Serial.printf("Detected Home Assistant online status, delaying discovery for %u ms...\n", randomDelay);
      delay(randomDelay);
      discoveryReported = MQTTReportDiscovery();
      if (discoveryReported)
      {
        Serial.println("Discovery messages sent!");
      }
      else
      {
        Serial.println("ERROR: Failure while (re-)sending discovery messages!");
      }
    }
    else if (strcmp(message, "offline") == 0)
    {
      Serial.println("Detected Home Assistant offline status!");
      discoveryReported = false;
    }
    else
    {
      Serial.print("WARNING: Unhandled \"homeassistant/status\" payload: ");
      Serial.println(message);
    }
  }
  else // Process all other MQTT messages.
  {
    if (strcmp(topic, concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicFront, "/set", "")) == 0) // Process "<root>/<device>/main/set"
    {                                                                                                                       // Process JSON for main set command
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err)
      {
        Serial.print("WARNING: JSON deserialization error in main/set: ");
        Serial.println(err.c_str());
        return;
      }
      if (doc["state"].is<const char *>())
      {
        MQTTCommandMainPower = (strcmp(doc["state"].as<const char *>(), MQTT_STATE_ON) == 0);
        MQTTCommandMainPowerReceived = true;
      }
      if (doc["brightness"].is<int>())
      {
        MQTTCommandMainBrightness = doc["brightness"];
        MQTTCommandMainBrightnessReceived = true;
      }
      if (doc["effect"].is<const char *>())
      {
        MQTTCommandMainGraphic = tfts.nameToClockFace(doc["effect"]);
        MQTTCommandMainGraphicReceived = true;
      }
    }
    else
    {
      if (strcmp(topic, concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBack, "/set", "")) == 0) // Process back/set
      {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload, length);
        if (err)
        {
          Serial.print("WARNING: JSON deserialization error in back/set: ");
          Serial.println(err.c_str());
          return;
        }
        if (doc["state"].is<const char *>())
        {
          MQTTCommandBackPower = (strcmp(doc["state"].as<const char *>(), MQTT_STATE_ON) == 0);
          MQTTCommandBackPowerReceived = true;
        }
        if (doc["brightness"].is<int>())
        {
          MQTTCommandBackBrightness = doc["brightness"];
          MQTTCommandBackBrightnessReceived = true;
        }
        if (doc["effect"].is<const char *>())
        {
          strncpy(MQTTCommandBackPattern, doc["effect"], sizeof(MQTTCommandBackPattern) - 1);
          MQTTCommandBackPattern[sizeof(MQTTCommandBackPattern) - 1] = '\0';
          MQTTCommandBackPatternReceived = true;
        }
        if (doc["color"].is<JsonObject>())
        {
          MQTTCommandBackColorPhase = backlights.hueToPhase(doc["color"]["h"]);
          MQTTCommandBackColorPhaseReceived = true;
        }
      }
      else
      {
        if (strcmp(topic, concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", Topic12hr, "/set", "")) == 0) // Process 12hr/set
        {
          JsonDocument doc;
          DeserializationError err = deserializeJson(doc, payload, length);
          if (err)
          {
            Serial.print("WARNING: JSON error in use_twelve_hours/set: ");
            Serial.println(err.c_str());
            return;
          }
          if (doc["state"].is<const char *>())
          {
            MQTTCommandUseTwelveHours = (strcmp(doc["state"].as<const char *>(), MQTT_STATE_ON) == 0);
            MQTTCommandUseTwelveHoursReceived = true;
          }
        }
        else
        {
          if (strcmp(topic, concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBlank0, "/set", "")) == 0) // Process blank0/set
          {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err)
            {
              Serial.print("WARNING: JSON error in blank_zero_hours/set: ");
              Serial.println(err.c_str());
              return;
            }
            if (doc["state"].is<const char *>())
            {
              MQTTCommandBlankZeroHours = (strcmp(doc["state"].as<const char *>(), MQTT_STATE_ON) == 0);
              MQTTCommandBlankZeroHoursReceived = true;
            }
          }
          else
          {
            if (strcmp(topic, concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicPulse, "/set", "")) == 0) // Process pulse/set
            {
              JsonDocument doc;
              DeserializationError err = deserializeJson(doc, payload, length);
              if (err)
              {
                Serial.print("WARNING: JSON error in pulse_bpm/set: ");
                Serial.println(err.c_str());
                return;
              }
              if (doc["state"].is<uint8_t>())
              {
                MQTTCommandPulseBpm = doc["state"];
                MQTTCommandPulseBpmReceived = true;
              }
            }
            else
            {
              if (strcmp(topic, concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBreath, "/set", "")) == 0) // Process breath/set
              {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, payload, length);
                if (err)
                {
                  Serial.print("WARNING: JSON error in breath_bpm/set: ");
                  Serial.println(err.c_str());
                  return;
                }
                if (doc["state"].is<uint8_t>())
                {
                  MQTTCommandBreathBpm = doc["state"];
                  MQTTCommandBreathBpmReceived = true;
                }
              }
              else
              {
                if (strcmp(topic, concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicRainbow, "/set", "")) == 0) // Process rainbow/set
                {
                  JsonDocument doc;
                  DeserializationError err = deserializeJson(doc, payload, length);
                  if (err)
                  {
                    Serial.print("WARNING: JSON error in rainbow_duration/set: ");
                    Serial.println(err.c_str());
                    return;
                  }
                  if (doc["state"].is<float>())
                  {
                    MQTTCommandRainbowSec = doc["state"];
                    MQTTCommandRainbowSecReceived = true;
                  }
                }
                else
                {
                  Serial.print("WARNING: Unhandled MQTT topic: ");
                  Serial.println(topic);
                }
              }
            }
          }
        }
      }
    }
  }
#endif // MQTT_HOME_ASSISTANT

#ifdef DEBUG_OUTPUT_MQTT
  Serial.println("DEBUG: Exiting MQTTCallback...");
#endif
} // end of MQTTCallback

void MQTTLoopFrequently()
{
  MQTTclient.loop();
  checkIfMQTTIsConnected();
}

void MQTTLoopInFreeTime()
{
  MQTTReportBackOnChange();
  MQTTPeriodicReportBack();
}

#ifdef MQTT_PLAIN_ENABLED
void MQTTReportStatus(bool forceUpdate)
{
  if ((LastSentStatus != MQTTStatusState) || forceUpdate)
  {
    char message[5];
    snprintf(message, sizeof(message), "%d", MQTTStatusState);
#ifdef MQTT_CLIENT_ID_FOR_SMARTNEST
    MQTTPublish(concat7_into(outbuf, UniqueDeviceName, "/report/state", "", "", "", "", ""), message, MQTT_RETAIN_STATE_MESSAGES);
#else
    MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceName, "/report/setpoint", "", "", ""), message, MQTT_RETAIN_STATE_MESSAGES);
#endif // MQTT_CLIENT_ID_FOR_SMARTNEST
    LastSentStatus = MQTTStatusState;
  }
}

void MQTTReportPowerState(bool forceUpdate)
{
  if ((MQTTStatusMainPower != LastSentMainPowerState) || forceUpdate)
  {
#ifdef MQTT_CLIENT_ID_FOR_SMARTNEST
    MQTTPublish(concat7_into(outbuf, UniqueDeviceName, "/report/powerState", "", "", "", "", ""), MQTTStatusMainPower == 0 ? MQTT_STATE_OFF : MQTT_STATE_ON, MQTT_RETAIN_STATE_MESSAGES);
#else
    MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceName, "/report/powerState", "", "", ""), MQTTStatusMainPower == 0 ? MQTT_STATE_OFF : MQTT_STATE_ON, MQTT_RETAIN_STATE_MESSAGES);
#endif // MQTT_CLIENT_ID_FOR_SMARTNEST
    LastSentMainPowerState = MQTTStatusMainPower;
  }
}

void MQTTReportWiFiSignal()
{
  char signal[5];
  int SignalLevel = WiFi.RSSI();
  // Ignore deviations smaller than 3 dBm.
  if (abs(SignalLevel - LastSentSignalLevel) > 2)
  {
    snprintf(signal, sizeof(signal), "%d", SignalLevel);
#ifdef MQTT_CLIENT_ID_FOR_SMARTNEST
    MQTTPublish(concat7_into(outbuf, UniqueDeviceName, "/report/signal", "", "", "", "", ""), signal, MQTT_RETAIN_STATE_MESSAGES);
#else
    MQTTPublish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceName, "/report/signal", "", "", ""), signal, MQTT_RETAIN_STATE_MESSAGES); // Reports the signal strength
#endif // MQTT_CLIENT_ID_FOR_SMARTNEST
    LastSentSignalLevel = SignalLevel;
  }
}
#endif // MQTT_PLAIN_ENABLED

void MQTTReportBackEverything(bool forceUpdateEverything)
{
  if (MQTTclient.connected())
  {
#ifdef MQTT_PLAIN_ENABLED
    if (!availabilityReported)
      MQTTReportAvailability(MQTT_ALIVE_MSG_ONLINE);
    MQTTReportPowerState(forceUpdateEverything);
    MQTTReportStatus(forceUpdateEverything);
    MQTTReportWiFiSignal();
#endif

#ifdef MQTT_HOME_ASSISTANT
    MQTTReportState(forceUpdateEverything);
#endif

    lastTimeSent = millis();
  }
}

void MQTTReportBackOnChange()
{
  if (MQTTclient.connected())
  {
#ifdef MQTT_PLAIN_ENABLED
    MQTTReportPowerState(false);
    MQTTReportStatus(false);
#endif
#ifdef MQTT_HOME_ASSISTANT
    // Home Assistant reporting
    if (!discoveryReported) // Check if discovery messages are already sent
    {
#ifdef DEBUG_OUTPUT_MQTT
      Serial.println("");
      Serial.println("DEBUG: Discovery messages not sent yet!");
      Serial.println("DEBUG: Sending discovery messages...");
#endif
      discoveryReported = MQTTReportDiscovery();
      if (!discoveryReported)
      {
        Serial.println("ERROR: Failure while sending discovery messages!");
      }
    }
    MQTTReportState(false); // Report only the device states which changed
#endif
  }
}

void MQTTPeriodicReportBack()
{ // Report/Send all device states with a limiter to not report too often.
  if (((millis() - lastTimeSent) > (MQTT_REPORT_STATUS_EVERY_SEC * 1000)) && MQTTclient.connected())
  {
#ifdef DEBUG_OUTPUT_MQTT
    Serial.println("");
    Serial.println("DEBUG: Sending periodic MQTT report...");
#endif
    MQTTConnected = MQTTclient.connected(); // Check regularly if still connected to the MQTT broker
#ifdef MQTT_HOME_ASSISTANT
    if (!discoveryReported) // Check if discovery messages are already sent
    {
#ifdef DEBUG_OUTPUT_MQTT
      Serial.println("");
      Serial.println("DEBUG: Discovery messages not sent yet!");
      Serial.println("DEBUG: Sending discovery messages...");
#endif
      discoveryReported = MQTTReportDiscovery();
      if (!discoveryReported)
      {
        Serial.println("ERROR: Failure while sending discovery messages!");
      }
    }
#endif
    MQTTReportBackEverything(true); // Report all device states
  }
}

#ifdef MQTT_HOME_ASSISTANT
bool MQTTReportDiscovery()
{
  JsonDocument discovery;

  // Build human readable device name. Default = plain model name.
  // Define ENABLE_HA_DEVICE_NAME_SUFFIX to append short MAC suffix for disambiguation when multiple identical models exist.
  char DeviceNameForHA[96];
#ifdef ENABLE_HA_DEVICE_NAME_SUFFIX
  const char *dash = strrchr(UniqueDeviceName, '-');
  if (dash && *(dash + 1) != '\0')
  {
    // Use everything after last '-' of UniqueDeviceName as short id (already hex upper-case)
    snprintf(DeviceNameForHA, sizeof(DeviceNameForHA), "%s (%s)", DEVICE_MODEL, dash + 1);
  }
  else
  {
    snprintf(DeviceNameForHA, sizeof(DeviceNameForHA), "%s", DEVICE_MODEL);
  }
#else
  snprintf(DeviceNameForHA, sizeof(DeviceNameForHA), "%s", DEVICE_MODEL);
#endif // ENABLE_HA_DEVICE_NAME_SUFFIX

  // Main Light.
  discovery.clear();
  discovery["device"]["identifiers"][0] = UniqueDeviceName;
  discovery["device"]["manufacturer"] = DEVICE_MANUFACTURER;
  discovery["device"]["model"] = DEVICE_MODEL;
  discovery["device"]["name"] = DeviceNameForHA;
  discovery["device"]["sw_version"] = FIRMWARE_VERSION;
  discovery["device"]["hw_version"] = DEVICE_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["unique_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicFront, "", "", "", "");
  discovery["object_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicFront, "", "", "", "");
  discovery["availability_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", "");
  discovery["name"] = "Main";
  discovery["icon"] = "mdi:clock-digital";
  discovery["schema"] = "json";
  discovery["state_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicFront, "", "");
  discovery["json_attributes_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicFront, "", "");
  discovery["command_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicFront, "/set", "");
  discovery["supported_color_modes"][0] = "brightness";
  discovery["brightness"] = true;
  discovery["brightness_scale"] = MQTT_BRIGHTNESS_MAIN_MAX;
  discovery["effect"] = true;
  for (size_t i = 1; i <= tfts.NumberOfClockFaces; i++)
  {
    discovery["effect_list"][i - 1] = tfts.clockFaceToName(i);
  }

  delay(150);
  if (!MQTTPublish(concat7_into(outbuf, "homeassistant/light/", UniqueDeviceNameLower, "/", TopicFront, "/config", "", ""), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Back Light.
  discovery.clear();
  discovery["device"]["identifiers"][0] = UniqueDeviceName;
  discovery["device"]["manufacturer"] = DEVICE_MANUFACTURER;
  discovery["device"]["model"] = DEVICE_MODEL;
  discovery["device"]["name"] = DeviceNameForHA;
  discovery["device"]["sw_version"] = FIRMWARE_VERSION;
  discovery["device"]["hw_version"] = DEVICE_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["unique_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicBack, "", "", "", "");
  discovery["object_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicBack, "", "", "", "");
  discovery["availability_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", "");
  discovery["name"] = "Back";
  discovery["icon"] = "mdi:television-ambient-light";
  discovery["schema"] = "json";
  discovery["state_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBack, "", "");
  discovery["json_attributes_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBack, "", "");
  discovery["command_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBack, "/set", "");
  discovery["brightness"] = true;
  discovery["brightness_scale"] = MQTT_BRIGHTNESS_BACK_MAX;
  discovery["supported_color_modes"][0] = "hs";
  discovery["effect"] = true;
  for (size_t i = 0; i < backlights.num_patterns; i++)
  {
    discovery["effect_list"][i] = backlights.patterns_str[i];
  }

  delay(150);
  if (!MQTTPublish(concat7_into(outbuf, "homeassistant/light/", UniqueDeviceNameLower, "/", TopicBack, "/config", "", ""), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Use Twelve Hours.
  discovery.clear();
  discovery["device"]["identifiers"][0] = UniqueDeviceName;
  discovery["device"]["manufacturer"] = DEVICE_MANUFACTURER;
  discovery["device"]["model"] = DEVICE_MODEL;
  discovery["device"]["name"] = DeviceNameForHA;
  discovery["device"]["sw_version"] = FIRMWARE_VERSION;
  discovery["device"]["hw_version"] = DEVICE_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["unique_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", Topic12hr, "", "", "", "");
  discovery["object_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", Topic12hr, "", "", "", "");
  discovery["availability_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", "");
  discovery["entity_category"] = "config";
  discovery["name"] = "Use Twelve Hours";
  discovery["icon"] = "mdi:timeline-clock";
  discovery["state_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", Topic12hr, "", "");
  discovery["json_attributes_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", Topic12hr, "", "");
  discovery["command_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", Topic12hr, "/set", "");
  discovery["value_template"] = "{{ value_json.state }}";
  discovery["state_on"] = "ON";
  discovery["state_off"] = "OFF";
  discovery["payload_on"] = "{\"state\":\"ON\"}";
  discovery["payload_off"] = "{\"state\":\"OFF\"}";

  delay(150);
  if (!MQTTPublish(concat7_into(outbuf, "homeassistant/switch/", UniqueDeviceNameLower, "/", Topic12hr, "/config", "", ""), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Blank Zero Hours.
  discovery.clear();
  discovery["device"]["identifiers"][0] = UniqueDeviceName;
  discovery["device"]["manufacturer"] = DEVICE_MANUFACTURER;
  discovery["device"]["model"] = DEVICE_MODEL;
  discovery["device"]["name"] = DeviceNameForHA;
  discovery["device"]["sw_version"] = FIRMWARE_VERSION;
  discovery["device"]["hw_version"] = DEVICE_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["unique_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicBlank0, "", "", "", "");
  discovery["object_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicBlank0, "", "", "", "");
  discovery["availability_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", "");
  discovery["entity_category"] = "config";
  discovery["name"] = "Blank Zero Hours";
  discovery["icon"] = "mdi:keyboard-space";
  discovery["state_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBlank0, "", "");
  discovery["json_attributes_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBlank0, "", "");
  discovery["command_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBlank0, "/set", "");
  discovery["value_template"] = "{{ value_json.state }}";
  discovery["state_on"] = "ON";
  discovery["state_off"] = "OFF";
  discovery["payload_on"] = "{\"state\":\"ON\"}";
  discovery["payload_off"] = "{\"state\":\"OFF\"}";

  delay(150);
  if (!MQTTPublish(concat7_into(outbuf, "homeassistant/switch/", UniqueDeviceNameLower, "/", TopicBlank0, "/config", "", ""), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Pulses per minute.
  discovery.clear();
  discovery["device"]["identifiers"][0] = UniqueDeviceName;
  discovery["device"]["manufacturer"] = DEVICE_MANUFACTURER;
  discovery["device"]["model"] = DEVICE_MODEL;
  discovery["device"]["name"] = DeviceNameForHA;
  discovery["device"]["sw_version"] = FIRMWARE_VERSION;
  discovery["device"]["hw_version"] = DEVICE_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["device_class"] = "speed";
  discovery["unique_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicPulse, "", "", "", "");
  discovery["object_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicPulse, "", "", "", "");
  discovery["availability_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", "");
  discovery["entity_category"] = "config";
  discovery["name"] = "Pulse, bpm";
  discovery["icon"] = "mdi:led-on";
  discovery["state_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicPulse, "", "");
  discovery["json_attributes_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicPulse, "", "");
  discovery["command_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicPulse, "/set", "");
  discovery["command_template"] = "{\"state\":{{value}}}";
  discovery["step"] = 1;
  discovery["min"] = 20;
  discovery["max"] = 120;
  discovery["mode"] = "slider";
  discovery["value_template"] = "{{ value_json.state }}";

  delay(150);
  if (!MQTTPublish(concat7_into(outbuf, "homeassistant/number/", UniqueDeviceNameLower, "/", TopicPulse, "/config", "", ""), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Breaths per minute.
  discovery.clear();
  discovery["device"]["identifiers"][0] = UniqueDeviceName;
  discovery["device"]["manufacturer"] = DEVICE_MANUFACTURER;
  discovery["device"]["model"] = DEVICE_MODEL;
  discovery["device"]["name"] = DeviceNameForHA;
  discovery["device"]["sw_version"] = FIRMWARE_VERSION;
  discovery["device"]["hw_version"] = DEVICE_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["device_class"] = "frequency";
  discovery["unique_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicBreath, "", "", "", "");
  discovery["object_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicBreath, "", "", "", "");
  discovery["availability_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", "");
  discovery["entity_category"] = "config";
  discovery["name"] = "Breath, bpm";
  discovery["icon"] = "mdi:cloud";
  discovery["state_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBreath, "", "");
  discovery["json_attributes_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBreath, "", "");
  discovery["command_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicBreath, "/set", "");
  discovery["command_template"] = "{\"state\":{{value}}}";
  discovery["step"] = 1;
  discovery["min"] = 5;
  discovery["max"] = 60;
  discovery["mode"] = "slider";
  discovery["value_template"] = "{{ value_json.state }}";

  delay(150);
  if (!MQTTPublish(concat7_into(outbuf, "homeassistant/number/", UniqueDeviceNameLower, "/", TopicBreath, "/config", "", ""), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Rainbow duration.
  discovery.clear();
  discovery["device"]["identifiers"][0] = UniqueDeviceName;
  discovery["device"]["manufacturer"] = DEVICE_MANUFACTURER;
  discovery["device"]["model"] = DEVICE_MODEL;
  discovery["device"]["name"] = DeviceNameForHA;
  discovery["device"]["sw_version"] = FIRMWARE_VERSION;
  discovery["device"]["hw_version"] = DEVICE_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["device_class"] = "duration";
  discovery["unique_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicRainbow, "", "", "", "");
  discovery["object_id"] = concat7_into(outbuf, UniqueDeviceNameLower, "_", TopicRainbow, "", "", "", "");
  discovery["availability_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", "");
  discovery["entity_category"] = "config";
  discovery["name"] = "Rainbow, sec";
  discovery["icon"] = "mdi:looks";
  discovery["state_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicRainbow, "", "");
  discovery["json_attributes_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicRainbow, "", "");
  discovery["command_topic"] = concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", TopicRainbow, "/set", "");
  discovery["command_template"] = "{\"state\":{{value}}}";
  discovery["step"] = 0.1;
  discovery["min"] = 0.2;
  discovery["max"] = 10;
  discovery["mode"] = "slider";
  discovery["value_template"] = "{{ value_json.state }}";

  delay(150);
  if (!MQTTPublish(concat7_into(outbuf, "homeassistant/number/", UniqueDeviceNameLower, "/", TopicRainbow, "/config", "", ""), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  discovery.clear();
  delay(120);
  MQTTReportAvailability(MQTT_ALIVE_MSG_ONLINE); // Publish online status

  return true;
}
#endif // MQTT_HOME_ASSISTANT

bool MQTTReportAvailability(const char *status)
{
#ifdef MQTT_CLIENT_ID_FOR_SMARTNEST
  availabilityReported = MQTTclient.publish(concat7_into(outbuf, UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", "", "", ""), status, MQTT_RETAIN_ALIVE_MESSAGES); // normally published with 'retain' flag set to true
#else
  availabilityReported = MQTTclient.publish(concat7_into(outbuf, MQTT_ROOT_TOPIC, "/", UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", ""), status, MQTT_RETAIN_ALIVE_MESSAGES); // normally published with 'retain' flag set to true}
#endif // MQTT_CLIENT_ID_FOR_SMARTNEST
#ifdef DEBUG_OUTPUT_MQTT
  Serial.print("DEBUG: Sent availability: ");
  Serial.print(concat7_into(outbuf, UniqueDeviceNameLower, "/", MQTT_ALIVE_TOPIC, "", "", "", ""));
  Serial.print(" ");
  Serial.println(status);
#endif
  return availabilityReported;
}

double round1(double value) // Helper function to round a double to one decimal place
{
  return (int)(value * 10 + 0.5) / 10.0;
}

bool endsWith(const char *str, const char *suffix) // Helper function to check if 'str' ends with 'suffix'
{
  if (!str || !suffix)
    return false;
  size_t strLen = strlen(str);
  size_t suffixLen = strlen(suffix);
  if (suffixLen > strLen)
    return false;
  return (strcmp(str + (strLen - suffixLen), suffix) == 0);
}

#endif // defined (MQTT_PLAIN_ENABLED) || defined (MQTT_HOME_ASSISTANT)
