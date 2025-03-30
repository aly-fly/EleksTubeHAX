#ifndef MQTT_client_H_
#define MQTT_client_H_

#include "GLOBAL_DEFINES.h"
//#include <FS.h>

#ifdef MQTT_ENABLED

#define MQTT_ALIVE_TOPIC "status"      // availability_topic :: https://www.home-assistant.io/integrations/mqtt/#availability_topic
#define MQTT_ALIVE_MSG_ONLINE "online" // default in HA. If changed, configure "payload_available" and "payload_not_available"
#define MQTT_ALIVE_MSG_OFFLINE "offline"
#define MQTT_RETAIN_ALIVE_MESSAGES true

#define TopicHAstatus "homeassistant/status"

#define MQTT_STATE_ON "ON"
#define MQTT_STATE_OFF "OFF"

// #define MQTT_BRIGHTNESS_MIN 0
// #define MQTT_BRIGHTNESS_MAX 255

// #define MQTT_ITENSITY_MIN 0
// #define MQTT_ITENSITY_MAX 7

extern bool MQTTConnected;

// commands from server
extern bool MQTTCommandPower;
extern bool MQTTCommandMainPower;
extern bool MQTTCommandBackPower;
extern bool MQTTCommandPowerReceived;
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

// status to server
extern bool MQTTStatusPower;
extern bool MQTTStatusMainPower;
extern bool MQTTStatusBackPower;
extern int MQTTStatusState;
// extern int MQTTStatusBattery;
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

// functions
void MQTTStart();
void MQTTLoopFrequently();
void MQTTLoopInFreeTime();
void MQTTReportBackEverything(bool force);

// unused functions
// void MQTTStop();

#endif // MQTT_ENABLED

#endif /* MQTT_client_H_ */
