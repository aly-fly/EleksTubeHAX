/*
  IPGeolocation.cpp - Library to get Geolocation data from https://IPGeolocation.io or https://app.abstractapi.com/api/ip-geolocation/
  Created by Dushyant Ahuja, September 2019.
  Released into the public domain.
  Modified by Aljaz Ogrin.
*/

#ifndef IPGeolocation_h
#define IPGeolocation_h

#include <Arduino.h>

#define DEBUG
#define GEO_CONN_TIMEOUT_SEC 15

#ifndef DEBUGPRINT
#ifdef DEBUG
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
  IPGeolocation(String Key, String API); // Use IPG for api.ipgeolocation.io and ABSTRACT for app.abstractapi.com/api/ip-geolocation
  bool updateStatus(IPGeo *I);
  String getResponse();

private:
  String _Key;
  String _Response;
  String _API;
};

#endif
