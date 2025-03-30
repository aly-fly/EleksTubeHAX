/*
 * Author: Aljaz Ogrin
 * Project: Alternative firmware for EleksTube IPS clock
 * Original location: https://github.com/aly-fly/EleksTubeHAX
 * Hardware: ESP32
 * File description: Connects to the MQTT Broker "smartnest.cz". Uses a device "Thermostat".
 * Sends status and receives commands from WebApp, Android app or connected devices (SmartThings, Google assistant, Alexa, etc.)
 * Configuration: open file "GLOBAL_DEFINES.h"
 * Reference: https://github.com/aososam/Smartnest/tree/master/Devices/thermostat
 * Documentation: https://www.docu.smartnest.cz/
 */

#include "MQTT_client_ips.h"
#ifdef MQTT_ENABLED
#include "WiFi.h"         // for ESP32
#include <PubSubClient.h> // Download and install this library first from: https://www.arduinolibraries.info/libraries/pub-sub-client
#include <ArduinoJson.h>
#include "TempSensor.h"
#include "TFTs.h"
#include "Backlights.h"
#include "Clock.h"
#ifdef MQTT_USE_TLS
#include <WiFiClientSecure.h> // for secure WiFi client

WiFiClientSecure espClient;
#else
WiFiClient espClient;
#endif // MQTT_USE_TLS
PubSubClient MQTTclient(espClient);

#define concat2(first, second) first second
#define concat3(first, second, third) first second third
#define concat4(first, second, third, fourth) first second third fourth
#define concat5(first, second, third, fourth, fifth) first second third fourth fifth

#define MQTT_STATE_ON "ON"
#define MQTT_STATE_OFF "OFF"

#define MQTT_BRIGHTNESS_MIN 0
#define MQTT_BRIGHTNESS_MAX 255

#define MQTT_ITENSITY_MIN 0
#define MQTT_ITENSITY_MAX 7

// private:
void MQTTCallback(char *topic, byte *payload, unsigned int length);
void checkIfMQTTIsConnected();

bool MQTTPublish(const char *Topic, const char *Message, const bool Retain);
bool MQTTPublish(const char *Topic, JsonDocument *Json, const bool Retain);

void MQTTReportState(bool forceUpdateEverything);
void MQTTProcessCommand();
void MQTTReportBattery();
void MQTTReportStatus();
void MQTTReportPowerState();
void MQTTReportWiFiSignal();
void MQTTReportTemperature();
void MQTTReportNotification(String message);
void MQTTReportGraphic(bool forceUpdateEverything);
void MQTTReportBackOnChange();
void MQTTReportBackEverything(bool forceUpdateEverything);
void MQTTPeriodicReportBack();
bool MQTTReportDiscovery();
void MQTTReportAvailability(const char *status);

// helper functions
double round1(double value);
bool endsWith(const char *str, const char *suffix);
#ifdef MQTT_USE_TLS
bool loadCARootCert();
#endif // MQTT_USE_TLS

char topic[100];
char msg[5];
uint32_t lastTimeSent = (uint32_t)(MQTT_REPORT_STATUS_EVERY_SEC * -1000);
uint8_t LastNotificationChecksum = 0;
uint32_t LastTimeTriedToConnect = 0;

bool MQTTConnected = true; // skip error message if disabled
bool discoveryReported = false;

// ========== APPLICATION VARIABLES ==========

// commands from server for pure MQTT
bool MQTTCommandPower = true;
bool MQTTCommandPowerReceived = false;
int MQTTCommandState = 1;
bool MQTTCommandStateReceived = false;

// commands from server for HA
#define TopicFront "main"
#define TopicBack "back"
#define Topic12hr "use_twelve_hours"
#define TopicBlank0 "blank_zero_hours"
#define TopicPulse "pulse_bpm"
#define TopicBreath "breath_bpm"
#define TopicRainbow "rainbow_duration"

bool MQTTCommandMainPower = true;
bool MQTTCommandBackPower = true;
bool MQTTCommandMainPowerReceived = false;
bool MQTTCommandBackPowerReceived = false;
bool MQTTCommandUseTwelveHours = false;
bool MQTTCommandUseTwelveHoursReceived = false;
bool MQTTCommandBlankZeroHours = false;
bool MQTTCommandBlankZeroHoursReceived = false;

uint8_t MQTTCommandBrightness = -1;
uint8_t MQTTCommandMainBrightness = -1;
uint8_t MQTTCommandBackBrightness = -1;
bool MQTTCommandBrightnessReceived = false;

bool MQTTCommandMainBrightnessReceived = false;
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

// status to server from Home Assistant
bool MQTTStatusMainPower = true;
bool MQTTStatusBackPower = true;
bool MQTTStatusUseTwelveHours = true;
bool MQTTStatusBlankZeroHours = true;

// status to server
bool MQTTStatusPower = true;

int MQTTStatusState = 0;
int MQTTStatusBattery = 7;
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

int LastSentSignalLevel = 999;
int LastSentPowerState = -1;
int LastSentMainPowerState = -1;
int LastSentBackPowerState = -1;
int LastSentStatus = -1;
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

double round1(double value)
{
  return (int)(value * 10 + 0.5) / 10.0;
}

bool MQTTPublish(const char *Topic, const char *Message, const bool Retain)
{
  if (!MQTTclient.connected())
    return false;

  bool ok = MQTTclient.publish(Topic, Message, Retain);

#ifdef DEBUG_OUTPUT
  if (ok)
  {
    Serial.print("TX MQTT: ");
    Serial.print(Topic);
    Serial.print(" - ");
    Serial.println(Message);
  }
  else
  {
    Serial.print("TX MQTT ERROR: ");
    Serial.println(Topic);
  }
#endif
  return ok;
}

bool MQTTPublish(const char *Topic, JsonDocument *Json, const bool Retain)
{
  size_t buffSize = measureJson(*Json) + 3; // Discovery Light = about 720 bytes
#ifdef DEBUG_OUTPUT
  Serial.printf("JSON size = %d\n", buffSize);
#endif
  char *buffer = (char *)malloc(buffSize);
  if (buffer == NULL)
  {
    Serial.printf("Error allocating %d bytes to serialize JSON.\n", buffSize);
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
    Serial.println("Error serializing JSON data.");
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
  /*
    if (forceUpdateEverything || !availabilityReported)
    {
      if (!MQTTPublish(concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC), MQTT_ALIVE_MSG_ONLINE, MQTT_RETAIN_ALIVE_MESSAGES))
        return;
      availabilityReported = true;
    }
  */
  if (forceUpdateEverything || MQTTStatusMainPower != LastSentMainPowerState || MQTTStatusMainBrightness != LastSentMainBrightness || MQTTStatusMainGraphic != LastSentMainGraphic)
  {
    JsonDocument state;
    state["state"] = MQTTStatusMainPower == 0 ? MQTT_STATE_OFF : MQTT_STATE_ON;
    state["brightness"] = MQTTStatusMainBrightness;
    state["effect"] = tfts.clockFaceToName(MQTTStatusMainGraphic);

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

void MQTTStart()
{
  MQTTConnected = false;
  if (((millis() - LastTimeTriedToConnect) > (MQTT_RECONNECT_WAIT_SEC * 100)) || (LastTimeTriedToConnect == 0))
  {
    LastTimeTriedToConnect = millis();
    MQTTclient.setServer(MQTT_BROKER, MQTT_PORT);
    MQTTclient.setCallback(MQTTCallback);
    MQTTclient.setBufferSize(2048);
#ifdef MQTT_USE_TLS
    bool result = loadCARootCert();
    if (!result)
    {
      return; // load certificate failed -> do not continue
    }
#endif

    Serial.println("");
    Serial.println("Connecting to MQTT...");
    // Attempt to connect. Set the last will (LWT) message, if the connection get lost
    if (MQTTclient.connect(MQTT_CLIENT,                                 // MQTT client id
                           MQTT_USERNAME,                               // MQTT username
                           MQTT_PASSWORD,                               // MQTT password
                           concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC), //// last will topic
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
      if (MQTTclient.state() == 5) // special error code for MQTT connection not allowed on smartnest.cz
      {
        Serial.println("Error: Connection not allowed by broker, possible reasons:");
        Serial.println("- Device is already online. Wait some seconds until it appears offline");
        Serial.println("- Wrong Username or password. Check credentials");
        Serial.println("- Client Id does not belong to this username, verify ClientId");
      }
      else // other error codes while connecting
      {
        Serial.println("Error: Not possible to connect to Broker!");
        Serial.print("Error code:");
        Serial.println(MQTTclient.state());
      }
      return; // do not continue if not connected
    }

#ifndef MQTT_HOME_ASSISTANT
    char subscribeTopic[100];
    snprintf(subscribeTopic, sizeof(subscribeTopic), "%s/#", MQTT_CLIENT);
    MQTTclient.subscribe(subscribeTopic); // Subscribes to all messages send to the device

    MQTTPublish("report/online", "true", MQTT_RETAIN_STATE_MESSAGES);                                // Reports that the device is online
    MQTTPublish("report/firmware", FIRMWARE_VERSION, MQTT_RETAIN_STATE_MESSAGES);                    // Reports the firmware version
    MQTTPublish("report/ip", (char *)WiFi.localIP().toString().c_str(), MQTT_RETAIN_STATE_MESSAGES); // Reports the ip
    MQTTPublish("report/network", (char *)WiFi.SSID().c_str(), MQTT_RETAIN_STATE_MESSAGES);          // Reports the network name
    MQTTReportWiFiSignal();
#endif

#ifdef MQTT_HOME_ASSISTANT
#ifdef MQTT_HOME_ASSISTANT_DISCOVERY
    MQTTclient.subscribe(TopicHAstatus); // Subscribe to homeassistant/status for receiving LWT and Birth messages from Home Assistant
#endif
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicFront, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicBack, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", Topic12hr, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicBlank0, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicBreath, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicPulse, "/set"));
    MQTTclient.subscribe(concat4(MQTT_CLIENT, "/", TopicRainbow, "/set"));
#endif // MQTT_HOME_ASSISTANT
  }
}

void checkIfMQTTIsConnected()
{
  MQTTConnected = MQTTclient.connected();
  if (!MQTTConnected)
  {
    MQTTStart(); // Try to reconnect to MQTT broker
  }
}

void MQTTStop(void)
{
  MQTTPublish(concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC), MQTT_ALIVE_MSG_OFFLINE, MQTT_RETAIN_ALIVE_MESSAGES);
  delay(100);
  MQTTclient.disconnect();
}

// Helper function to check if 'str' ends with 'suffix'
bool endsWith(const char *str, const char *suffix)
{
  if (!str || !suffix)
    return false;
  size_t strLen = strlen(str);
  size_t suffixLen = strlen(suffix);
  if (suffixLen > strLen)
    return false;
  return (strcmp(str + (strLen - suffixLen), suffix) == 0);
}

void MQTTCallback(char *topic, byte *payload, unsigned int length)
{
#ifdef DEBUG_OUTPUT
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

#ifdef DEBUG_OUTPUT
  Serial.println("DEBUG: Converted payload to char array:");
  Serial.println(message);
  Serial.println("DEBUG: Processing MQTT message...");
  Serial.print("DEBUG: RX MQTT: ");
  Serial.print(topic);
  Serial.print(" ");
  Serial.println(message);
#endif

#ifndef MQTT_HOME_ASSISTANT
  // Check if topic ends with "/directive/powerState"
  if (endsWith(topic, "/directive/powerState"))
  {
    // Turn On or OFF based on payload
    if (strcmp(message, "ON") == 0)
    {
      MQTTCommandPower = true;
      MQTTCommandPowerReceived = true;
    }
    else if (strcmp(message, "OFF") == 0)
    {
      MQTTCommandPower = false;
      MQTTCommandPowerReceived = true;
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
#endif // NOT defined MQTT_HOME_ASSISTANT

#ifdef MQTT_HOME_ASSISTANT
  // Process "homeassistant/status"
  if (strcmp(topic, TopicHAstatus) == 0)
  {
    if (strcmp(message, "online") == 0)
    {
      Serial.println("Detected Home Assistant online status.");
#ifdef MQTT_HOME_ASSISTANT_DISCOVERY
      uint16_t randomDelay = random(100, 400);
      Serial.print("Delaying discovery for ");
      Serial.print(randomDelay);
      Serial.println(" ms.");
      delay(randomDelay);
      discoveryReported = MQTTReportDiscovery();
#endif
    }
    else if (strcmp(message, "offline") == 0)
    {
      Serial.println("Detected Home Assistant offline status.");
    }
    else
    {
      Serial.print("Unhandled homeassistant/status payload: ");
      Serial.println(message);
    }
  }
  else
  {
    // Process "<MQTT_CLIENT>/main/set"
    if (strcmp(topic, concat4(MQTT_CLIENT, "/", TopicFront, "/set")) == 0)
    {
      // Process JSON for main set command
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
    {
      // Process "<MQTT_CLIENT>/back/set"
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
      {
        // Process "<MQTT_CLIENT>/use_twelve_hours/set"
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
        {
          // Process "<MQTT_CLIENT>/blank_zero_hours/set"
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
          {
            // Process "<MQTT_CLIENT>/pulse_bpm/set"
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
            {
              // Process "<MQTT_CLIENT>/breath_bpm/set"
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
              {
                // Process "<MQTT_CLIENT>/rainbow_duration/set"
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
                  Serial.print("Warning: Unhandled MQTT topic: ");
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

#ifdef DEBUG_OUTPUT
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

void MQTTReportBattery()
{
  char message[5];
  snprintf(message, sizeof(message), "%d", MQTTStatusBattery);
  MQTTPublish("report/battery", message, MQTT_RETAIN_STATE_MESSAGES);
}

void MQTTReportStatus()
{
  if (LastSentStatus != MQTTStatusState)
  {
    char message[5];
    snprintf(message, sizeof(message), "%d", MQTTStatusState);
    MQTTPublish("report/setpoint", message, MQTT_RETAIN_STATE_MESSAGES);
    LastSentStatus = MQTTStatusState;
  }
}

void MQTTReportTemperature()
{
#ifdef ONE_WIRE_BUS_PIN
  if (fTemperature > -30)
  { // transmit data to MQTT only if data is valid
    MQTTPublish("report/temperature", sTemperatureTxt);
  }
#endif
}

void MQTTReportPowerState()
{
  if (MQTTStatusPower != LastSentPowerState)
  {
    MQTTPublish("report/powerState", MQTTStatusPower == 0 ? MQTT_STATE_OFF : MQTT_STATE_ON, MQTT_RETAIN_STATE_MESSAGES);

    LastSentPowerState = MQTTStatusPower;
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
    MQTTPublish("report/signal", signal, MQTT_RETAIN_STATE_MESSAGES); // Reports the signal strength
    LastSentSignalLevel = SignalLevel;
  }
}

void MQTTReportNotification(String message)
{
  int i;
  byte NotificationChecksum = 0;
  for (i = 0; i < message.length(); i++)
  {
    NotificationChecksum += byte(message[i]);
  }
  // send only different notification, do not re-send same notifications!
  if (NotificationChecksum != LastNotificationChecksum)
  {
    // string to char array
    char msg2[message.length() + 1];
    strncpy(msg2, message.c_str(), sizeof(msg2) - 1);
    MQTTPublish("report/notification", msg2, MQTT_RETAIN_STATE_MESSAGES);
    LastNotificationChecksum = NotificationChecksum;
  }
}

void MQTTReportGraphic(bool forceUpdateEverything)
{
  if (forceUpdateEverything || MQTTStatusGraphic != LastSentGraphic)
  {
    char graphic[3]; // Increased size to accommodate null terminator
    snprintf(graphic, sizeof(graphic), "%i", MQTTStatusGraphic);
    MQTTPublish("graphic", graphic, MQTT_RETAIN_STATE_MESSAGES); // Reports the signal strength

    LastSentGraphic = MQTTStatusGraphic;
  }
}

void MQTTReportBackEverything(bool forceUpdateEverything)
{
  if (MQTTclient.connected())
  {
#ifndef MQTT_HOME_ASSISTANT
    MQTTReportPowerState();
    MQTTReportStatus();
    // MQTTReportBattery();
    MQTTReportWiFiSignal();
    MQTTReportTemperature();
#endif

#ifdef MQTT_HOME_ASSISTANT
    MQTTReportState(forceUpdateEverything);
#endif

    lastTimeSent = millis();
  }
}

bool MQTTReportDiscovery()
{
#ifdef MQTT_HOME_ASSISTANT_DISCOVERY
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
  discovery["brightness"] = true;
  discovery["brightness_scale"] = 255;
  discovery["effect"] = true;
  for (size_t i = 1; i <= tfts.NumberOfClockFaces; i++)
  {
    discovery["effect_list"][i - 1] = tfts.clockFaceToName(i);
  }

  delay(120);
  if (!MQTTPublish(concat5("homeassistant/light/", MQTT_CLIENT, "_", TopicFront, "/light/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    ;
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
  discovery["brightness_scale"] = 7;
  discovery["effect"] = true;
  for (size_t i = 0; i < backlights.num_patterns; i++)
  {
    discovery["effect_list"][i] = backlights.patterns_str[i];
  }
  discovery["supported_color_modes"][0] = "hs";

  delay(120);
  if (!MQTTPublish(concat5("homeassistant/light/", MQTT_CLIENT, "_", TopicBack, "/light/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    ;
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

  delay(120);
  if (!MQTTPublish(concat5("homeassistant/switch/", MQTT_CLIENT, "_", Topic12hr, "/switch/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    ;
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

  delay(120);
  if (!MQTTPublish(concat5("homeassistant/switch/", MQTT_CLIENT, "_", TopicBlank0, "/switch/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    ;
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

  delay(120);
  if (!MQTTPublish(concat5("homeassistant/number/", MQTT_CLIENT, "_", TopicPulse, "/number/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    ;
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

  delay(120);
  if (!MQTTPublish(concat5("homeassistant/number/", MQTT_CLIENT, "_", TopicBreath, "/number/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    ;
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

  delay(120);
  if (!MQTTPublish(concat5("homeassistant/number/", MQTT_CLIENT, "_", TopicRainbow, "/number/config"), &discovery, MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES))
    ;
  return false;

  discovery.clear();
  delay(120);
  MQTTReportAvailability(MQTT_ALIVE_MSG_ONLINE); // Publish online status
#endif
  return true;
}

void MQTTReportBackOnChange()
{
  if (MQTTclient.connected())
  {
#ifndef MQTT_HOME_ASSISTANT
    MQTTReportPowerState();
    MQTTReportStatus();
#endif
#ifdef MQTT_HOME_ASSISTANT_DISCOVERY
    if (!discoveryReported)
    {
      discoveryReported = MQTTReportDiscovery();
    }
#endif
#ifdef MQTT_HOME_ASSISTANT
    MQTTReportState(false);
#endif
  }
}

void MQTTPeriodicReportBack()
{
  if (((millis() - lastTimeSent) > (MQTT_REPORT_STATUS_EVERY_SEC * 1000)) && MQTTclient.connected())
  {
    MQTTConnected = MQTTclient.connected();
#ifdef MQTT_HOME_ASSISTANT_DISCOVERY
    if (!discoveryReported)
    {
      discoveryReported = MQTTReportDiscovery();
    }
#endif
    MQTTReportBackEverything(true);
  }
}

void MQTTReportAvailability(const char *status)
{
  MQTTclient.publish(concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC), status, MQTT_RETAIN_ALIVE_MESSAGES); // Publish with retained flag (true)
#ifdef DEBUG_OUTPUT
  Serial.print("DEBUG: Sent availability: ");
  Serial.print(concat3(MQTT_CLIENT, "/", MQTT_ALIVE_TOPIC));
  Serial.print(" ");
  Serial.println(status);
#endif
}

#endif // MQTT_ENABLED