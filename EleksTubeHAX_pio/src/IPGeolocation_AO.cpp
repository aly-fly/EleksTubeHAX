/*
 * Library taken from: https://github.com/dushyantahuja/IPGeolocation and heavily modified.
 * - added error checking
 * - cleaned code
 * - updated connection to server
 * - configured for use on ESP32
 */

#include "Arduino.h"
#include <WiFi.h> // ESP32
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "IPGeolocation_AO.h"

IPGeolocation::IPGeolocation(String Key)
{
  _Key = Key;
  _API = "IPG";
}
IPGeolocation::IPGeolocation(String Key, String API)
{
  _Key = Key;
  _API = API;
}

String IPGeolocation::getResponse()
{
  return _Response;
}

bool IPGeolocation::updateStatus(IPGeo *I)
{
  if (_API == "ABSTRACT")
  {

    // https://ipgeolocation.abstractapi.com/v1/?api_key=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

    const char *host = "ipgeolocation.abstractapi.com";
    const int httpsPort = 443; // HTTPS= 443 and HTTP = 80
    WiFiClientSecure httpsClient;
    httpsClient.setInsecure();                           // skip verification
    httpsClient.setTimeout(GEO_CONN_TIMEOUT_SEC * 1000); // 15 Seconds

    DEBUGPRINT("HTTPS Connecting");
    int r = 0; // retry counter
    while ((!httpsClient.connect(host, httpsPort)) && (r < 10))
    {
      delay(200);
      DEBUGPRINT(".");
      r++;
    }
    if (!httpsClient.connected())
    {
      DEBUGPRINT("Connection unsucessful!");
      return false;
    }
    else
    {
      DEBUGPRINT("Connected.");
    }

    String Link = String("https://") + host + "/v1/?api_key=" + _Key;

    DEBUGPRINT("requesting URL: ");
    DEBUGPRINT(String("GET ") + Link + " HTTP/1.0");
    httpsClient.println(String("GET ") + Link + " HTTP/1.1");
    httpsClient.println(String("Host: ") + host);
    httpsClient.println(String("Connection: close"));
    httpsClient.println();

    DEBUGPRINT("Request sent, waiting for response");

    //    DEBUGPRINT("headers:");
    uint32_t StartTime = millis();
    bool response_ok = false;
    //    String _Response; // use private var
    while (httpsClient.connected())
    {
      _Response = httpsClient.readStringUntil('\n');
      //      DEBUGPRINT(_Response);
      if (_Response == "\r")
      {
        DEBUGPRINT("headers received");
        response_ok = true;
        break;
      }
      if (millis() - StartTime > GEO_CONN_TIMEOUT_SEC * 1000)
      {
        response_ok = false;
        break;
      }
    }
    if (!response_ok)
    {
      DEBUGPRINT("Error reading header data from server.");
      return false;
    }

    DEBUGPRINT("reply:");
    StartTime = millis();
    response_ok = false;
    while (httpsClient.connected())
    {
      _Response = httpsClient.readString();
      DEBUGPRINT(_Response); // Print response
      response_ok = true;
      //      break;
      if (millis() - StartTime > GEO_CONN_TIMEOUT_SEC * 1000)
      {
        response_ok = false;
        break;
      }
    }
    if (!response_ok)
    {
      DEBUGPRINT("Error reading json data from server.");
      return false;
    }

    JsonDocument doc;
    deserializeJson(doc, _Response);

    // catch errors:
    if (_Response.indexOf("error") > 0)
    {
      DEBUGPRINT("IP Geoloc ERROR!");
      return false;
    }

    /* SAMPLES:
    failure:
    {"error":{"message":"Invalid API key provided.","code":"unauthorized","details":null}}

    winter time:
    {"ip_address":"93.103.xxx.xxx",
    "city":"Kranj",
    "city_geoname_id":3197378,
    "region":"Kranj",
    "region_iso_code":"052",
    "region_geoname_id":3197377,
    "postal_code":"4000",
    "country":"Slovenia",
    "country_code":"SI",
    "country_geoname_id":3190538,
    "country_is_eu":true,
    "continent":"Europe",
    "continent_code":"EU",
    "continent_geoname_id":6255148,
    "longitude":14.3556,
    "latitude":46.2389,
    "security":{"is_vpn":false},
    "timezone":{"name":"Europe/Ljubljana","abbreviation":"CET","gmt_offset":1,"current_time":"17:58:18","is_dst":false},
    "flag":{"emoji":"đź‡¸đź‡®","unicode":"U+1F1F8 U+1F1EE","png":"https://static.abstractapi.com/country-flags/SI_flag.png","svg":"https://static.abstractapi.com/country-flags/SI_flag.svg"},
    "currency":{"currency_name":"Euros","currency_code":"EUR"},
    "connection":{"autonomous_system_number":34779,"autonomous_system_organization":"xxxxxxxx","connection_type":"Cellular","isp_name":"xxxxxxxxxx","organization_name":null}}

    summer time:
    {"ip_address":"93.103.xxx.xxx",
    "city":"Kranj",
    "city_geoname_id":3197378,
    "region":"Kranj",
    "region_iso_code":"052",
    "region_geoname_id":3197377,
    "postal_code":"4000",
    "country":"Slovenia",
    "country_code":"SI",
    "country_geoname_id":3190538,
    "country_is_eu":true,
    "continent":"Europe",
    "continent_code":"EU",
    "continent_geoname_id":6255148,
    "longitude":14.3556,
    "latitude":46.2389,
    "security":{"is_vpn":false},
    "timezone":{"name":"Europe/Ljubljana","abbreviation":"CEST","gmt_offset":2,"current_time":"23:39:52","is_dst":true},
    "flag":{"emoji":"đź‡¸đź‡®","unicode":"U+1F1F8 U+1F1EE","png":"https://static.abstractapi.com/country-flags/SI_flag.png","svg":"https://static.abstractapi.com/country-flags/SI_flag.svg"},
    "currency":{"currency_name":"Euros","currency_code":"EUR"},
    "connection":{"autonomous_system_number":34779,"autonomous_system_organization":"xxxx","connection_type":"Cellular","isp_name":"xxxxx","organization_name":null}}
    */

    JsonObject timezone = doc["timezone"];

    I->tz = timezone["name"].as<String>();
    I->is_dst = timezone["is_dst"];
    I->offset = timezone["gmt_offset"];
    I->current_time = timezone["current_time"].as<String>();
    I->country = doc["country"].as<String>();
    I->country_code = doc["country_code"].as<String>();
    I->city = doc["city"].as<String>();
    I->latitude = doc["latitude"];
    I->longitude = doc["longitude"];

    DEBUGPRINT("Geo Time Zone: ");
    DEBUGPRINT(I->tz);
    DEBUGPRINT("Geo Current Time: ");
    DEBUGPRINT(I->current_time);
    return true;
  }
  else
  {
    /*  UNIFINISHED BUGGY CODE
    const char *host = "api.ipgeolocation.io";
    const int httpsPort = 443;  //HTTPS= 443 and HTTP = 80
    WiFiClientSecure httpsClient;
    const size_t capacity = JSON_OBJECT_SIZE(10) + JSON_OBJECT_SIZE(17) + 580;
    httpsClient.setInsecure();
    httpsClient.setTimeout(15000); // 15 Seconds
    delay(1000);

    DEBUGPRINT("HTTPS Connecting");
    int r=0; //retry counter
    while((!httpsClient.connect(host, httpsPort)) && (r < 30)){
        delay(100);
        DEBUGPRINT(".");
        r++;
    }

    String Link;

    //GET Data
    Link = "/timezone?apiKey="+_Key;

    DEBUGPRINT("requesting URL: ");
    DEBUGPRINT(host+Link);

    httpsClient.print(String("GET ") + Link + " HTTP/1.1\r\n" +
                "Host: " + host + "\r\n" +
                "Connection: close\r\n\r\n");

    DEBUGPRINT("request sent");

    while (httpsClient.connected()) {
      _Response = httpsClient.readStringUntil('\n');
      if (_Response == "\r") {
        DEBUGPRINT("headers received");
        break;
      }
    }
    DEBUGPRINT("reply was:");
    DEBUGPRINT("==========");
    httpsClient.readStringUntil('\n'); // The API sends an extra line with just a number. This breaks the JSON parsing, hence an extra read
    while(httpsClient.connected()){
      _Response = httpsClient.readString();
      DEBUGPRINT(_Response); //Print response
    }
    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, _Response);
    I->tz = doc["timezone"].as<String>();
    I->is_dst = doc["is_dst"];
    int dst_savings = doc["dst_savings"].as<int>();
    double timezone_offset = doc["timezone_offset"];
    I->offset= timezone_offset + ((I->is_dst) ? dst_savings : 0);
    I->country = doc["geo"]["country_name"].as<String>();
    I->country_code = doc["geo"]["country_code2"].as<String>();
    I->city = doc["geo"]["city"].as<String>();
    I->latitude = doc["geo"]["latitude"];
    I->longitude = doc["geo"]["longitude"];

    DEBUGPRINT("Time Zone: ");
    DEBUGPRINT(I->tz);
    DEBUGPRINT("DST Savings: ");
    DEBUGPRINT(dst_savings);
  */
  }
  return false;
}
