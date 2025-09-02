#ifndef MQTT_client_H_
#define MQTT_client_H_

#include "GLOBAL_DEFINES.h"

#ifdef MQTT_USE_TLS
#include "SPIFFS.h"
#endif // MQTT_USE_TLS

#ifdef MQTT_PLAIN_ENABLED
#define MQTT_ALIVE_TOPIC "report/online"
#define MQTT_ALIVE_MSG_ONLINE "true"
#define MQTT_ALIVE_MSG_OFFLINE "false"

#define MQTT_RETAIN_ALIVE_MESSAGES true
#define MQTT_RETAIN_STATE_MESSAGES false
#endif // MQTT_PLAIN_ENABLED

#ifdef MQTT_HOME_ASSISTANT
#define MQTT_ALIVE_TOPIC "status"      // Availability_topic (https://www.home-assistant.io/integrations/mqtt/#availability_topic)
#define MQTT_ALIVE_MSG_ONLINE "online" // Default in HA. If changed, configure "payload_available" and "payload_not_available".
#define MQTT_ALIVE_MSG_OFFLINE "offline"

// The alive message is sent to the broker when the device connects to the MQTT broker and when it disconnects from the broker.
// The messages should be retained by default, but you can change this with the setting below.
#define MQTT_RETAIN_ALIVE_MESSAGES true // Alive messages should be retained
// The state/status messages sent to the broker should not be retained by default, because the values should be "fresh", but you can change this with the setting below.
#define MQTT_RETAIN_STATE_MESSAGES false // Set to true if you want to retain the state/status messages in the MQTT broker (default: false)

#define MQTT_TOPIC_HASTATUS "homeassistant/status"
// The discovery messages are sent to Home Assistant to automatically discover the device and its entities.
// The messages should be retained by default, but you can change this with the setting below.
// Retained messages can create ghost entities that keep coming back (for example if you change MQTT_CLIENT). You need to delete them manually from the broker queue!
#define MQTT_HOME_ASSISTANT_RETAIN_DISCOVERY_MESSAGES true // Discovery messages are retained by default in HA

#define MQTT_BRIGHTNESS_MAIN_MAX 255
#define MQTT_BRIGHTNESS_BACK_MAX 7
#endif // NOT MQTT_HOME_ASSISTANT

#define MQTT_STATE_ON "ON"
#define MQTT_STATE_OFF "OFF"

extern bool MQTTConnected;

// Commands from server.
extern bool MQTTCommandMainPower;
extern bool MQTTCommandBackPower;
extern bool MQTTCommandMainPowerReceived;
extern bool MQTTCommandBackPowerReceived;
extern int MQTTCommandState;
extern bool MQTTCommandStateReceived;
extern uint8_t MQTTCommandBrightness;
extern uint8_t MQTTCommandMainBrightness;
extern uint8_t MQTTCommandBackBrightness;
extern bool MQTTCommandBrightnessReceived;
extern bool MQTTCommandMainBrightnessReceived;
extern bool MQTTCommandBackBrightnessReceived;
extern char MQTTCommandPattern[];
extern char MQTTCommandBackPattern[];
extern bool MQTTCommandPatternReceived;
extern bool MQTTCommandBackPatternReceived;
extern uint16_t MQTTCommandBackColorPhase;
extern bool MQTTCommandBackColorPhaseReceived;
extern uint8_t MQTTCommandGraphic;
extern uint8_t MQTTCommandMainGraphic;
extern bool MQTTCommandGraphicReceived;
extern bool MQTTCommandMainGraphicReceived;
extern bool MQTTCommandUseTwelveHours;
extern bool MQTTCommandUseTwelveHoursReceived;
extern bool MQTTCommandBlankZeroHours;
extern bool MQTTCommandBlankZeroHoursReceived;
extern uint8_t MQTTCommandPulseBpm;
extern bool MQTTCommandPulseBpmReceived;
extern uint8_t MQTTCommandBreathBpm;
extern bool MQTTCommandBreathBpmReceived;
extern float MQTTCommandRainbowSec;
extern bool MQTTCommandRainbowSecReceived;

// Status to server.
extern bool MQTTStatusMainPower;
extern bool MQTTStatusBackPower;
extern int MQTTStatusState;
extern uint8_t MQTTStatusBrightness;
extern uint8_t MQTTStatusMainBrightness;
extern uint8_t MQTTStatusBackBrightness;
extern char MQTTStatusPattern[];
extern char MQTTStatusBackPattern[];
extern uint16_t MQTTStatusBackColorPhase;
extern uint8_t MQTTStatusGraphic;
extern uint8_t MQTTStatusMainGraphic;
extern bool MQTTStatusUseTwelveHours;
extern bool MQTTStatusBlankZeroHours;
extern uint8_t MQTTStatusPulseBpm;
extern uint8_t MQTTStatusBreathBpm;
extern float MQTTStatusRainbowSec;

bool MQTTStart(bool restart);
void MQTTLoopFrequently();
void MQTTLoopInFreeTime();
void MQTTReportBackEverything(bool force);

#endif /* MQTT_client_H_ */
