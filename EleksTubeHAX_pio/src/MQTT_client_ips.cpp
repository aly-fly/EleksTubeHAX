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
#include "WiFi.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "TFTs.h"
#include "Backlights.h"
#include "Clock.h"
#ifdef MQTT_USE_TLS
#include <WiFiClientSecure.h> // for secure WiFi client

WiFiClientSecure espClient;
#else
WiFiClient espClient;
#endif // MQTT_USE_TLS

// initialize the MQTT client
PubSubClient MQTTclient(espClient);

// functions for general MQTT handling
void MQTTCallback(char *topic, byte *payload, unsigned int length);
void checkIfMQTTIsConnected();
bool MQTTPublish(const char *Topic, const char *Message, const bool Retain);
bool MQTTPublish(const char *Topic, JsonDocument *Json, const bool Retain);
void MQTTReportState(bool forceUpdateEverything);
void MQTTReportBackOnChange();
void MQTTReportBackEverything(bool forceUpdateEverything);
void MQTTPeriodicReportBack();

// plain MQTT mode functions
void MQTTReportPowerState(bool forceUpdate);
void MQTTReportWiFiSignal();
void MQTTReportStatus(bool forceUpdate);

// Home Assistant mode functions
bool MQTTReportDiscovery();
bool MQTTReportAvailability(const char *status);

// helper functions
double round1(double value);
bool endsWith(const char *str, const char *suffix);
#ifdef MQTT_USE_TLS
bool loadCARootCert();
#endif // MQTT_USE_TLS

// variables
uint32_t lastTimeSent = (uint32_t)(MQTT_REPORT_STATUS_EVERY_SEC * -1000);
uint32_t LastTimeTriedToConnect = 0;

bool MQTTConnected = false;     // Show connection status on the clock's LCD
bool discoveryReported = false; // initial state of discovery messages sent to HA
bool availabilityReported = false;

// commands from server for plain MQTT mode
int MQTTCommandState = 1;
bool MQTTCommandStateReceived = false;

#ifdef MQTT_HOME_ASSISTANT
// topics for HA
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

// status to server for Home Assistant
bool MQTTStatusMainPower = true;
bool MQTTStatusBackPower = true;
bool MQTTStatusUseTwelveHours = true;
bool MQTTStatusBlankZeroHours = true;

// status to server
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

// plain MQTT
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
    return;

  // send availability message
  if (forceUpdateEverything || !availabilityReported)
  {
    if (!MQTTReportAvailability(MQTT_ALIVE_MSG_ONLINE))
      return;
    availabilityReported = true;
  }

  if (forceUpdateEverything || MQTTStatusMainPower != LastSentMainPowerState || MQTTStatusMainBrightness != LastSentMainBrightness || MQTTStatusMainGraphic != LastSentMainGraphic)
  {
    JsonDocument state;
    state["state"] = MQTTStatusMainPower == 0 ? MQTT_STATE_OFF : MQTT_STATE_ON;
    state["brightness"] = MQTTStatusMainBrightness;
    state["effect"] = tfts.clockFaceToName(MQTTStatusMainGraphic);
    state["color_mode"] = "brightness";

    if (!MQTTPublish(concat3(MQTT_CLIENT, "/", TopicFront), &state, MQTT_RETAIN_STATE_MESSAGES))
      return;

    LastSentMainPowerState = MQTTStatusMainPower;
    LastSentMainBrightness = MQTTStatusMainBrightness;
    LastSentMainGraphic = MQTTStatusMainGraphic;
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

    if (!MQTTPublish(concat3(MQTT_CLIENT, "/", TopicBack), &state, MQTT_RETAIN_STATE_MESSAGES))
      return;

    LastSentBackPowerState = MQTTStatusBackPower;
    LastSentBackBrightness = MQTTStatusBackBrightness;
    strncpy(LastSentBackPattern, MQTTStatusBackPattern, sizeof(LastSentBackPattern) - 1);
    LastSentBackPattern[sizeof(LastSentBackPattern) - 1] = '\0';
    LastSentBackColorPhase = MQTTStatusBackColorPhase;
  }

  if (forceUpdateEverything || MQTTStatusUseTwelveHours != LastSentUseTwelveHours)
  {
    JsonDocument state;
    state["state"] = MQTTStatusUseTwelveHours ? MQTT_STATE_ON : MQTT_STATE_OFF;

    if (!MQTTPublish(concat3(MQTT_CLIENT, "/", Topic12hr), &state, MQTT_RETAIN_STATE_MESSAGES))
      return;

    LastSentUseTwelveHours = MQTTStatusUseTwelveHours;
  }

  if (forceUpdateEverything || MQTTStatusBlankZeroHours != LastSentBlankZeroHours)
  {
    JsonDocument state;
    state["state"] = MQTTStatusBlankZeroHours ? MQTT_STATE_ON : MQTT_STATE_OFF;

    if (!MQTTPublish(concat3(MQTT_CLIENT, "/", TopicBlank0), &state, MQTT_RETAIN_STATE_MESSAGES))
      return;

    LastSentBlankZeroHours = MQTTStatusBlankZeroHours;
  }

  if (forceUpdateEverything || MQTTStatusPulseBpm != LastSentPulseBpm)
  {
    JsonDocument state;
    state["state"] = MQTTStatusPulseBpm;

    if (!MQTTPublish(concat3(MQTT_CLIENT, "/", TopicPulse), &state, MQTT_RETAIN_STATE_MESSAGES))
      return;

    LastSentPulseBpm = MQTTStatusPulseBpm;
  }

  if (forceUpdateEverything || MQTTStatusBreathBpm != LastSentBreathBpm)
  {
    JsonDocument state;
    state["state"] = MQTTStatusBreathBpm;

    if (!MQTTPublish(concat3(MQTT_CLIENT, "/", TopicBreath), &state, MQTT_RETAIN_STATE_MESSAGES))
      return;

    LastSentBreathBpm = MQTTStatusBreathBpm;
  }

  if (forceUpdateEverything || MQTTStatusRainbowSec != LastSentRainbowSec)
  {

    JsonDocument state;
    state["state"] = round1(MQTTStatusRainbowSec);

    if (!MQTTPublish(concat3(MQTT_CLIENT, "/", TopicRainbow), &state, MQTT_RETAIN_STATE_MESSAGES))
      return;

    LastSentRainbowSec = MQTTStatusRainbowSec;
  }
#endif
}

#ifdef MQTT_USE_TLS
bool loadCARootCert()
{
  const char *filename = "/mqtt-ca-root.pem";
  Serial.println("Loading CA Root Certificate");

  // Check if the PEM file exists
  if (!SPIFFS.exists(filename))
  {
    Serial.println("ERROR: File not found mqtt-ca-root.pem");
    return false;
  }

  // Open the PEM file in read mode
  File file = SPIFFS.open(filename, "r");
  if (!file)
  {
    Serial.println("ERROR: Failed to open mqtt-ca-root.pem");
    return false;
  }

  // Get the size of the file
  size_t size = file.size();
  if (size == 0)
  {
    Serial.println("ERROR: Empty mqtt-ca-root.pem");
    file.close();
    return false;
  }

  // Use the loadCA() method to load the certificate directly from the file stream
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
  { // try to connect to MQTT broker only if the time since last connection attempt is greater than the defined wait time (default 30 sec)
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
    Serial.println("");
    Serial.println("Connecting to MQTT...");
    // Attempt to connect. Set the last will (LWT) message, if the connection get lost
    if (MQTTclient.connect(MQTT_CLIENT,                                 // MQTT client id
                           MQTT_USERNAME,                               // MQTT username
                           MQTT_PASSWORD,                               // MQTT password
                           concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC), // last will topic
                           0,                                           // last will QoS
                           MQTT_RETAIN_ALIVE_MESSAGES,                  // retain message
                           MQTT_ALIVE_MSG_OFFLINE))                     // last will message
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
      return false; // do not continue if not connected
    } // connect failed

#ifdef DEBUG_OUTPUT_MQTT
    Serial.println("DEBUG: subscribing to MQTT topics...");
#endif

#ifdef MQTT_PLAIN_ENABLED
    bool ok = MQTTclient.subscribe(concat2(MQTT_CLIENT, "/directive/#")); // Subscribes only to messages send to the device
    if (!ok)
      Serial.println("Error subscribing to /directive messages!");
#ifdef DEBUG_OUTPUT_MQTT
    Serial.println("DEBUG: Subscribed to /directive/# messages sent to the device.");
    Serial.println("DEBUG: Sending initial status messages...");
#endif
    // send initial status messages
    MQTTReportAvailability(MQTT_ALIVE_MSG_ONLINE);                                                                          // Reports that the device is online
    MQTTPublish(concat2(MQTT_CLIENT, "/report/firmware"), FIRMWARE_VERSION, MQTT_RETAIN_STATE_MESSAGES);                    // Reports the firmware version
    MQTTPublish(concat2(MQTT_CLIENT, "/report/ip"), (char *)WiFi.localIP().toString().c_str(), MQTT_RETAIN_STATE_MESSAGES); // Reports the ip
    MQTTPublish(concat2(MQTT_CLIENT, "/report/network"), (char *)WiFi.SSID().c_str(), MQTT_RETAIN_STATE_MESSAGES);          // Reports the network name
    MQTTReportWiFiSignal();
#endif // MQTT_PLAIN_ENABLED

#ifdef MQTT_HOME_ASSISTANT
    MQTTclient.subscribe(TopicHAstatus); // Subscribe to homeassistant/status for receiving LWT and Birth messages from Home Assistant
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicFront, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicBack, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", Topic12hr, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicBlank0, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicBreath, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicPulse, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicRainbow, "/set"));
#ifdef DEBUG_OUTPUT_MQTT
    Serial.println("DEBUG: subscribed to topics: ");
    Serial.print(concat4(MQTT_CLIENT, "/", TopicFront, "/set"));
    Serial.println(", ");
    Serial.print(concat4(MQTT_CLIENT, "/", TopicBack, "/set"));
    Serial.println(", ");
    Serial.print(concat4(MQTT_CLIENT, "/", Topic12hr, "/set"));
    Serial.println(", ");
    Serial.print(concat4(MQTT_CLIENT, "/", TopicBlank0, "/set"));
    Serial.println(", ");
    Serial.print(concat4(MQTT_CLIENT, "/", TopicBreath, "/set"));
    Serial.println(", ");
    Serial.print(concat4(MQTT_CLIENT, "/", TopicPulse, "/set"));
    Serial.println(", ");
    Serial.print(concat4(MQTT_CLIENT, "/", TopicRainbow, "/set"));
    Serial.println(", ");
    Serial.println(TopicHAstatus);
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

// void MQTTStop(void)
// {
//   MQTTReportAvailability(MQTT_ALIVE_MSG_OFFLINE);
//   delay(100);
//   MQTTclient.disconnect();
// }

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
    // in case the incoming payload exceeds our buffer size, truncate it
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
  // Check if topic ends with "/directive/powerState"
  if (endsWith(topic, "/directive/powerState"))
  {
    // Turn On or OFF based on payload
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
  if (strcmp(topic, TopicHAstatus) == 0) // Process "homeassistant/status" messages -> react if Home Assistant is online or offline

  {
    if (strcmp(message, "online") == 0)
    {
      Serial.println("Detected Home Assistant online status! Sending discovery messages!");
      uint16_t randomDelay = random(100, 400);
      Serial.print("Delaying discovery for ");
      Serial.print(randomDelay);
      Serial.println(" ms.");
      delay(randomDelay);
      discoveryReported = MQTTReportDiscovery();
      if (!discoveryReported)
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
  else // process all other MQTT messages
  {
    if (strcmp(topic, concat4(MQTT_CLIENT, "/", TopicFront, "/set")) == 0) // Process "<MQTT_CLIENT>/main/set"
    {                                                                      // Process JSON for main set command
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err)
      {
        Serial.print("DEBUG: JSON deserialization error in main/set: ");
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
    { // Process "<MQTT_CLIENT>/back/set"
      if (strcmp(topic, concat4(MQTT_CLIENT, "/", TopicBack, "/set")) == 0)
      {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload, length);
        if (err)
        {
          Serial.print("DEBUG: JSON deserialization error in back/set: ");
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
      { // Process "<MQTT_CLIENT>/use_twelve_hours/set"
        if (strcmp(topic, concat4(MQTT_CLIENT, "/", Topic12hr, "/set")) == 0)
        {
          JsonDocument doc;
          DeserializationError err = deserializeJson(doc, payload, length);
          if (err)
          {
            Serial.print("DEBUG: JSON error in use_twelve_hours/set: ");
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
        { // Process "<MQTT_CLIENT>/blank_zero_hours/set"
          if (strcmp(topic, concat4(MQTT_CLIENT, "/", TopicBlank0, "/set")) == 0)
          {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, payload, length);
            if (err)
            {
              Serial.print("DEBUG: JSON error in blank_zero_hours/set: ");
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
          { // Process "<MQTT_CLIENT>/pulse_bpm/set"
            if (strcmp(topic, concat4(MQTT_CLIENT, "/", TopicPulse, "/set")) == 0)
            {
              JsonDocument doc;
              DeserializationError err = deserializeJson(doc, payload, length);
              if (err)
              {
                Serial.print("DEBUG: JSON error in pulse_bpm/set: ");
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
            { // Process "<MQTT_CLIENT>/breath_bpm/set"
              if (strcmp(topic, concat4(MQTT_CLIENT, "/", TopicBreath, "/set")) == 0)
              {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, payload, length);
                if (err)
                {
                  Serial.print("DEBUG: JSON error in breath_bpm/set: ");
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
              { // Process "<MQTT_CLIENT>/rainbow_duration/set"
                if (strcmp(topic, concat4(MQTT_CLIENT, "/", TopicRainbow, "/set")) == 0)
                {
                  JsonDocument doc;
                  DeserializationError err = deserializeJson(doc, payload, length);
                  if (err)
                  {
                    Serial.print("DEBUG: JSON error in rainbow_duration/set: ");
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
    MQTTPublish(concat2(MQTT_CLIENT, "/report/setpoint"), message, MQTT_RETAIN_STATE_MESSAGES);
    // MQTTPublish(concat2(MQTT_CLIENT, "/report/temperature"), message, MQTT_RETAIN_STATE_MESSAGES);
    LastSentStatus = MQTTStatusState;
  }
}

void MQTTReportPowerState(bool forceUpdate)
{
  if ((MQTTStatusMainPower != LastSentMainPowerState) || forceUpdate)
  {
    MQTTPublish(concat2(MQTT_CLIENT, "/report/powerState"), MQTTStatusMainPower == 0 ? MQTT_STATE_OFF : MQTT_STATE_ON, MQTT_RETAIN_STATE_MESSAGES);
    LastSentMainPowerState = MQTTStatusMainPower;
  }
}

void MQTTReportWiFiSignal()
{
  char signal[5];
  int SignalLevel = WiFi.RSSI();
  // ignore deviations smaller than 3 dBm
  if (abs(SignalLevel - LastSentSignalLevel) > 2)
  {
    snprintf(signal, sizeof(signal), "%d", SignalLevel);
    MQTTPublish(concat2(MQTT_CLIENT, "/report/signal"), signal, MQTT_RETAIN_STATE_MESSAGES); // Reports the signal strength
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
      Serial.println("DEBUG: Disovery messages not sent yet!");
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
{ // Report/Send all device states with a limiter to not report too often
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
      Serial.println("DEBUG: Disovery messages not sent yet!");
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

  // Main Light
  discovery.clear();
  discovery["device"]["identifiers"][0] = MQTT_CLIENT;
  discovery["device"]["manufacturer"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MANUFACTURER;
  discovery["device"]["model"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["name"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["sw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_SW_VERSION;
  discovery["device"]["hw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["unique_id"] = concat3(MQTT_CLIENT, "_", TopicFront);
  discovery["object_id"] = concat3(MQTT_CLIENT, "_", TopicFront);
  discovery["availability_topic"] = concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC);
  discovery["name"] = "Main";
  discovery["icon"] = "mdi:clock-digital";
  discovery["schema"] = "json";
  discovery["state_topic"] = concat3(MQTT_CLIENT, "/", TopicFront);
  discovery["json_attributes_topic"] = concat3(MQTT_CLIENT, "/", TopicFront);
  discovery["command_topic"] = concat4(MQTT_CLIENT, "/", TopicFront, "/set");
  discovery["supported_color_modes"][0] = "brightness";
  discovery["brightness"] = true;
  discovery["brightness_scale"] = MQTT_BRIGHTNESS_MAIN_MAX;
  discovery["effect"] = true;
  for (size_t i = 1; i <= tfts.NumberOfClockFaces; i++)
  {
    discovery["effect_list"][i - 1] = tfts.clockFaceToName(i);
  }

  delay(150);
  if (!MQTTPublish(concat5("homeassistant/light/", MQTT_CLIENT, "_", TopicFront, "/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Back Light
  discovery.clear();
  discovery["device"]["identifiers"][0] = MQTT_CLIENT;
  discovery["device"]["manufacturer"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MANUFACTURER;
  discovery["device"]["model"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["name"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["sw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_SW_VERSION;
  discovery["device"]["hw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["unique_id"] = concat3(MQTT_CLIENT, "_", TopicBack);
  discovery["object_id"] = concat3(MQTT_CLIENT, "_", TopicBack);
  discovery["availability_topic"] = concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC);
  discovery["name"] = "Back";
  discovery["icon"] = "mdi:television-ambient-light";
  discovery["schema"] = "json";
  discovery["state_topic"] = concat3(MQTT_CLIENT, "/", TopicBack);
  discovery["json_attributes_topic"] = concat3(MQTT_CLIENT, "/", TopicBack);
  discovery["command_topic"] = concat4(MQTT_CLIENT, "/", TopicBack, "/set");
  discovery["brightness"] = true;
  discovery["brightness_scale"] = MQTT_BRIGHTNESS_BACK_MAX;
  discovery["supported_color_modes"][0] = "hs";
  discovery["effect"] = true;
  for (size_t i = 0; i < backlights.num_patterns; i++)
  {
    discovery["effect_list"][i] = backlights.patterns_str[i];
  }

  delay(150);
  if (!MQTTPublish(concat5("homeassistant/light/", MQTT_CLIENT, "_", TopicBack, "/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Use Twelwe Hours
  discovery.clear();
  discovery["device"]["identifiers"][0] = MQTT_CLIENT;
  discovery["device"]["manufacturer"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MANUFACTURER;
  discovery["device"]["model"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["name"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["sw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_SW_VERSION;
  discovery["device"]["hw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["unique_id"] = concat3(MQTT_CLIENT, "_", Topic12hr);
  discovery["object_id"] = concat3(MQTT_CLIENT, "_", Topic12hr);
  discovery["availability_topic"] = concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC);
  discovery["entity_category"] = "config";
  discovery["name"] = "Use Twelve Hours";
  discovery["icon"] = "mdi:timeline-clock";
  discovery["state_topic"] = concat3(MQTT_CLIENT, "/", Topic12hr);
  discovery["json_attributes_topic"] = concat3(MQTT_CLIENT, "/", Topic12hr);
  discovery["command_topic"] = concat4(MQTT_CLIENT, "/", Topic12hr, "/set");
  discovery["value_template"] = "{{ value_json.state }}";
  discovery["state_on"] = "ON";
  discovery["state_off"] = "OFF";
  discovery["payload_on"] = "{\"state\":\"ON\"}";
  discovery["payload_off"] = "{\"state\":\"OFF\"}";

  delay(150);
  if (!MQTTPublish(concat5("homeassistant/switch/", MQTT_CLIENT, "_", Topic12hr, "/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Blank Zero Hours
  discovery.clear();
  discovery["device"]["identifiers"][0] = MQTT_CLIENT;
  discovery["device"]["manufacturer"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MANUFACTURER;
  discovery["device"]["model"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["name"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["sw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_SW_VERSION;
  discovery["device"]["hw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["unique_id"] = concat3(MQTT_CLIENT, "_", TopicBlank0);
  discovery["object_id"] = concat3(MQTT_CLIENT, "_", TopicBlank0);
  discovery["availability_topic"] = concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC);
  discovery["entity_category"] = "config";
  discovery["name"] = "Blank Zero Hours";
  discovery["icon"] = "mdi:keyboard-space";
  discovery["state_topic"] = concat3(MQTT_CLIENT, "/", TopicBlank0);
  discovery["json_attributes_topic"] = concat3(MQTT_CLIENT, "/", TopicBlank0);
  discovery["command_topic"] = concat4(MQTT_CLIENT, "/", TopicBlank0, "/set");
  discovery["value_template"] = "{{ value_json.state }}";
  discovery["state_on"] = "ON";
  discovery["state_off"] = "OFF";
  discovery["payload_on"] = "{\"state\":\"ON\"}";
  discovery["payload_off"] = "{\"state\":\"OFF\"}";

  delay(150);
  if (!MQTTPublish(concat5("homeassistant/switch/", MQTT_CLIENT, "_", TopicBlank0, "/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Pulses per minute
  discovery.clear();
  discovery["device"]["identifiers"][0] = MQTT_CLIENT;
  discovery["device"]["manufacturer"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MANUFACTURER;
  discovery["device"]["model"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["name"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["sw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_SW_VERSION;
  discovery["device"]["hw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["device_class"] = "speed";
  discovery["unique_id"] = concat3(MQTT_CLIENT, "_", TopicPulse);
  discovery["object_id"] = concat3(MQTT_CLIENT, "_", TopicPulse);
  discovery["availability_topic"] = concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC);
  discovery["entity_category"] = "config";
  discovery["name"] = "Pulse, bpm";
  discovery["icon"] = "mdi:led-on";
  discovery["state_topic"] = concat3(MQTT_CLIENT, "/", TopicPulse);
  discovery["json_attributes_topic"] = concat3(MQTT_CLIENT, "/", TopicPulse);
  discovery["command_topic"] = concat4(MQTT_CLIENT, "/", TopicPulse, "/set");
  discovery["command_template"] = "{\"state\":{{value}}}";
  discovery["step"] = 1;
  discovery["min"] = 20;
  discovery["max"] = 120;
  discovery["mode"] = "slider";
  discovery["value_template"] = "{{ value_json.state }}";

  delay(150);
  if (!MQTTPublish(concat5("homeassistant/number/", MQTT_CLIENT, "_", TopicPulse, "/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Breathes per minute
  discovery.clear();
  discovery["device"]["identifiers"][0] = MQTT_CLIENT;
  discovery["device"]["manufacturer"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MANUFACTURER;
  discovery["device"]["model"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["name"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["sw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_SW_VERSION;
  discovery["device"]["hw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["device_class"] = "frequency";
  discovery["unique_id"] = concat3(MQTT_CLIENT, "_", TopicBreath);
  discovery["object_id"] = concat3(MQTT_CLIENT, "_", TopicBreath);
  discovery["availability_topic"] = concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC);
  discovery["entity_category"] = "config";
  discovery["name"] = "Breath, bpm";
  discovery["icon"] = "mdi:cloud";
  discovery["state_topic"] = concat3(MQTT_CLIENT, "/", TopicBreath);
  discovery["json_attributes_topic"] = concat3(MQTT_CLIENT, "/", TopicBreath);
  discovery["command_topic"] = concat4(MQTT_CLIENT, "/", TopicBreath, "/set");
  discovery["command_template"] = "{\"state\":{{value}}}";
  discovery["step"] = 1;
  discovery["min"] = 5;
  discovery["max"] = 60;
  discovery["mode"] = "slider";
  discovery["value_template"] = "{{ value_json.state }}";

  delay(150);
  if (!MQTTPublish(concat5("homeassistant/number/", MQTT_CLIENT, "_", TopicBreath, "/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  // Rainbow duration
  discovery.clear();
  discovery["device"]["identifiers"][0] = MQTT_CLIENT;
  discovery["device"]["manufacturer"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MANUFACTURER;
  discovery["device"]["model"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["name"] = MQTT_HOME_ASSISTANT_DISCOVERY_DEVICE_MODEL;
  discovery["device"]["sw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_SW_VERSION;
  discovery["device"]["hw_version"] = MQTT_HOME_ASSISTANT_DISCOVERY_HW_VERSION;
  discovery["device"]["connections"][0][0] = "mac";
  discovery["device"]["connections"][0][1] = WiFi.macAddress();
  discovery["device_class"] = "duration";
  discovery["unique_id"] = concat3(MQTT_CLIENT, "_", TopicRainbow);
  discovery["object_id"] = concat3(MQTT_CLIENT, "_", TopicRainbow);
  discovery["availability_topic"] = concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC);
  discovery["entity_category"] = "config";
  discovery["name"] = "Rainbow, sec";
  discovery["icon"] = "mdi:looks";
  discovery["state_topic"] = concat3(MQTT_CLIENT, "/", TopicRainbow);
  discovery["json_attributes_topic"] = concat3(MQTT_CLIENT, "/", TopicRainbow);
  discovery["command_topic"] = concat4(MQTT_CLIENT, "/", TopicRainbow, "/set");
  discovery["command_template"] = "{\"state\":{{value}}}";
  discovery["step"] = 0.1;
  discovery["min"] = 0.2;
  discovery["max"] = 10;
  discovery["mode"] = "slider";
  discovery["value_template"] = "{{ value_json.state }}";

  delay(150);
  if (!MQTTPublish(concat5("homeassistant/number/", MQTT_CLIENT, "_", TopicRainbow, "/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    return false;

  discovery.clear();
  delay(120);
  MQTTReportAvailability(MQTT_ALIVE_MSG_ONLINE); // Publish online status

  return true;
}
#endif // MQTT_HOME_ASSISTANT

bool MQTTReportAvailability(const char *status)
{
  availabilityReported = MQTTclient.publish(concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC), status, MQTT_RETAIN_ALIVE_MESSAGES); // normally published with 'retain' flag set to true
#ifdef DEBUG_OUTPUT_MQTT
  Serial.print("DEBUG: Sent availability: ");
  Serial.print(concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC));
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
