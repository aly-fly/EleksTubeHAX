/*
 * Code taken from: https://github.com/dushyantahuja/IPGeolocation and heavily modified.
 * Version from September 2019.
 *
 * Created by Dushyant Ahuja.
 * Released into the public domain.
 * Modified by Aljaz Ogrin and others.
 * - added error checking
 * - cleaned code
 * - updated connection to server
 * - configured for use on ESP32
 * - added support for multiple geolocation providers
 * - https://app.abstractapi.com/api/ip-geolocation/ (ABSTRACT)
 * - https://ip-api.com (IPAPI)
 * - https://api.ipgeolocation.io (IPINFO)
 */

#ifndef IPGeolocation_h
#define IPGeolocation_h

#include <Arduino.h>
#include "_USER_DEFINES.h"

// ************* Compile-time validation checks  *************
// Geolocation provider validation - ensure only one provider is selected
#if defined(GEOLOCATION_PROVIDER_IPAPI) && defined(GEOLOCATION_PROVIDER_ABSTRACTAPI)
#error "Multiple geolocation providers selected! Only one GEOLOCATION_PROVIDER_* can be active at a time."
#endif
#if defined(GEOLOCATION_PROVIDER_IPAPI) && defined(GEOLOCATION_PROVIDER_IPGEOLOCATION)
#error "Multiple geolocation providers selected! Only one GEOLOCATION_PROVIDER_* can be active at a time."
#endif
#if defined(GEOLOCATION_PROVIDER_ABSTRACTAPI) && defined(GEOLOCATION_PROVIDER_IPGEOLOCATION)
#error "Multiple geolocation providers selected! Only one GEOLOCATION_PROVIDER_* can be active at a time."
#endif
#if !defined(GEOLOCATION_PROVIDER_IPAPI) && !defined(GEOLOCATION_PROVIDER_ABSTRACTAPI) && !defined(GEOLOCATION_PROVIDER_IPGEOLOCATION) && defined(GEOLOCATION_ENABLED)
#warning "No geolocation provider selected! Defaulting to GEOLOCATION_PROVIDER_IPAPI. Please select one GEOLOCATION_PROVIDER_* explicitly."
#define GEOLOCATION_PROVIDER_IPAPI // Default fallback
#endif

#define GEO_CONN_TIMEOUT_SEC 15

#ifndef DEBUGPRINT
#ifdef DEBUG_OUTPUT_GEO
#define DEBUGPRINT(x) Serial.println(x)
#else
#define DEBUGPRINT(x)
#endif
#endif

struct IPGeo
{
  String tz;
  double offset;
  bool is_dst;
  String current_time;
  String city;
  String country;
  String country_code;
  double latitude;
  double longitude;
};

class IPGeolocation
{
public:
  explicit IPGeolocation(String Key);
  IPGeolocation(String Key, String API); // Use ABSTRACT for app.abstractapi.com, IPAPI for ip-api.com, and IPINFO for api.ipgeolocation.io
  bool updateStatus(IPGeo *I);
  String getResponse();

private:
  String _Key;
  String _Response;
  String _API;
};

#endif
