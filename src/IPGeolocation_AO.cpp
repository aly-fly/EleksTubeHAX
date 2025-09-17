/*
 * Library taken from: https://github.com/dushyantahuja/IPGeolocation and heavily modified.
 * - added error checking
 * - cleaned code
 * - updated connection to server
 * - configured for use on ESP32
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "IPGeolocation_AO.h"


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
    const char *host = "ipgeolocation.abstractapi.com";
    const int httpsPort = 443;
    WiFiClientSecure httpsClient;

    httpsClient.setInsecure(); // Skip verification
    httpsClient.setTimeout(GEO_CONN_TIMEOUT_SEC * 1000);

    DEBUGPRINT("HTTPS Connecting...");
    int r = 0; // Retry counter

    while ((!httpsClient.connect(host, httpsPort)) && (r < 10))
    {
      delay(200);
      DEBUGPRINT(".");
      r++;
    }
    if (!httpsClient.connected())
    {
      DEBUGPRINT("Connection unsuccessful!");

      return false;
    }
    else
    {
      DEBUGPRINT("Connected.");
    }

    String Link = String("https://") + host + "/v1/?api_key=" + _Key;

    DEBUGPRINT("requesting URL: ");
    DEBUGPRINT(String("Requesting URL: GET ") + Link + " HTTP/1.0");

    httpsClient.println(String("GET ") + Link + " HTTP/1.1");
    httpsClient.println(String("Host: ") + host);
    httpsClient.println(String("Connection: close"));
    httpsClient.println();

    DEBUGPRINT("Request sent, waiting for response...");

    uint32_t StartTime = millis();
    bool response_ok = false;
    while (httpsClient.connected())
    {
      _Response = httpsClient.readStringUntil('\n');
      if (_Response == "\r")
      {
        DEBUGPRINT("Headers received!");
        // DEBUGPRINT(_Response); // Print response

        response_ok = true;
        break;
      }
      if (millis() - StartTime > GEO_CONN_TIMEOUT_SEC * 1000)
      {
        Serial.printf("ERROR: Headers took too long (now %lu, was %lu, difference: %lu)!\n", millis(), StartTime, millis() - StartTime);

        response_ok = false;
        break;
      }
    }
    if (!response_ok)
    {
      DEBUGPRINT("ERROR: Error reading header data from server!");
      return false;
    }


    StartTime = millis();
    response_ok = false;
    while (httpsClient.connected())
    {
      _Response = httpsClient.readString();
      DEBUGPRINT("Response received!");
      // DEBUGPRINT(_Response); // Print response
      response_ok = true;
      break;

      if (millis() - StartTime > GEO_CONN_TIMEOUT_SEC * 1000)
      {
        Serial.printf("ERROR: Response took too long (now %lu, was %lu, difference: %lu)!\n", millis(), StartTime, millis() - StartTime);

        response_ok = false;
        break;
      }
    }
    if (!response_ok)
    {
      DEBUGPRINT("ERROR: Error reading json data from server!");
      return false;
    }

    JsonDocument doc;
    deserializeJson(doc, _Response);

    if (_Response.indexOf("error") > 0)
    {
      DEBUGPRINT("ERROR: IP Geolocation failed!");
      return false;
    }

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

    // DEBUGPRINT(String("Geo Time Zone: ") + I->tz);
    // DEBUGPRINT(String("Geo Current Time: ") + I->current_time);
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
