#ifndef WIFI_WPS_H
#define WIFI_WPS_H

#include "GLOBAL_DEFINES.h"

enum WifiState_t
{
    disconnected,
    connected,
    wps_active,
    wps_success,
    wps_failed,
    num_states
};
void WifiBegin();
void WiFiStartWps();
void WifiReconnect();

extern WifiState_t WifiState;

#endif // WIFI_WPS_H
