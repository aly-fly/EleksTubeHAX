/*
 * Library code taken from: https://github.com/dushyantahuja/IPGeolocation and heavily modified.
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
  if (_API == "ABSTRACTAPI")
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
      char ssl_error[128] = {0};
      int last_error = httpsClient.lastError(ssl_error, sizeof(ssl_error));
      DEBUGPRINT(String("lastError code: ") + last_error);
      if (last_error != 0 && ssl_error[0] != '\0')
      {
        DEBUGPRINT(String("lastError detail: ") + ssl_error);
      }
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

    DEBUGPRINT(String("Geo Time Zone: ") + I->tz);
    DEBUGPRINT(String("Geo Current Time: ") + I->current_time);
    DEBUGPRINT(String("Geo GMT Offset: ") + I->offset);
    DEBUGPRINT(String("Geo Is DST: ") + I->is_dst);
    DEBUGPRINT(String("Geo Country: ") + I->country);
    DEBUGPRINT(String("Geo Country Code: ") + I->country_code);
    DEBUGPRINT(String("Geo City: ") + I->city);
    DEBUGPRINT(String("Geo Latitude: ") + I->latitude);
    DEBUGPRINT(String("Geo Longitude: ") + I->longitude);

    return true;
  }
  else if (_API == "IPGEOLOCATION")
  {
    const char *host = "api.ipgeolocation.io";
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
      char ssl_error[128] = {0};
      int last_error = httpsClient.lastError(ssl_error, sizeof(ssl_error));
      DEBUGPRINT(String("lastError code: ") + last_error);
      if (last_error != 0 && ssl_error[0] != '\0')
      {
        DEBUGPRINT(String("lastError detail: ") + ssl_error);
      }
      return false;
    }
    else
    {
      DEBUGPRINT("Connected.");
    }

    // ipgeolocation.io timezone endpoint
    String Link = "/timezone?apiKey=" + _Key;

    DEBUGPRINT("requesting URL: ");
    DEBUGPRINT(String("Requesting URL: GET ") + Link + " HTTP/1.1");

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
    // ipgeolocation.io sends an extra line with just a number, we need to skip it
    httpsClient.readStringUntil('\n');

    while (httpsClient.connected())
    {
      _Response = httpsClient.readString();
      DEBUGPRINT("Response received!");
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

    // Check for error in response
    if (_Response.indexOf("error") > 0 || _Response.indexOf("message") > 0)
    {
      DEBUGPRINT("ERROR: ipgeolocation.io request failed!");
      DEBUGPRINT(_Response);
      return false;
    }

    // Parse ipgeolocation.io response format
    I->tz = doc["timezone"].as<String>();
    I->is_dst = doc["is_dst"];

    // Calculate total offset including DST
    double timezone_offset = doc["timezone_offset"];
    int dst_savings = doc["dst_savings"].as<int>();
    I->offset = timezone_offset + (I->is_dst ? dst_savings : 0);

    I->current_time = ""; // ipgeolocation.io timezone endpoint doesn't provide current time
    I->country = doc["geo"]["country_name"].as<String>();
    I->country_code = doc["geo"]["country_code2"].as<String>();
    I->city = doc["geo"]["city"].as<String>();
    I->latitude = doc["geo"]["latitude"];
    I->longitude = doc["geo"]["longitude"];

    DEBUGPRINT(String("IPGeo Current Time: Not provided by ipgeolocation.io!"));
    DEBUGPRINT(String("IPGeo Time Zone: ") + I->tz);
    DEBUGPRINT(String("IPGeo DST: ") + (I->is_dst ? "true" : "false"));
    DEBUGPRINT(String("IPGeo Calculated Offset: ") + I->offset);
    DEBUGPRINT(String("IPGeo DST Savings: ") + dst_savings);
    DEBUGPRINT(String("IPGeo Country: ") + I->country);
    DEBUGPRINT(String("IPGeo Country Code: ") + I->country_code);
    DEBUGPRINT(String("IPGeo City: ") + I->city);
    DEBUGPRINT(String("IPGeo Latitude: ") + I->latitude);
    DEBUGPRINT(String("IPGeo Longitude: ") + I->longitude);

    return true;
  }
  else if (_API == "IPAPI")
  {
    const char *host = "ip-api.com";
    const int httpPort = 80; // IP-API.com uses HTTP, not HTTPS for free tier
    WiFiClient httpClient;

    httpClient.setTimeout(GEO_CONN_TIMEOUT_SEC * 1000);

    DEBUGPRINT("HTTP Connecting...");
    int r = 0; // Retry counter

    while ((!httpClient.connect(host, httpPort)) && (r < 10))
    {
      delay(200);
      DEBUGPRINT(".");
      r++;
    }
    if (!httpClient.connected())
    {
      DEBUGPRINT("Connection unsuccessful!");
      return false;
    }
    else
    {
      DEBUGPRINT("Connected.");
    }

    // IP-API.com JSON endpoint with timezone and DST info
    String Link = "/json/?fields=status,country,countryCode,city,lat,lon,timezone,offset,dst,query";

    DEBUGPRINT("requesting URL: ");
    DEBUGPRINT(String("Requesting URL: GET ") + Link + " HTTP/1.1");

    // Send HTTP request with proper headers
    httpClient.print(String("GET ") + Link + " HTTP/1.1\r\n");
    httpClient.print(String("Host: ") + host + "\r\n");
    httpClient.print("User-Agent: ESP32-EleksTube/1.0\r\n");
    httpClient.print("Accept: application/json\r\n");
    httpClient.print("Connection: close\r\n");
    httpClient.print("\r\n");
    httpClient.flush(); // Make sure all data is sent

    DEBUGPRINT("Request sent, waiting for response...");

    // Wait for response with timeout
    uint32_t timeout = millis() + (GEO_CONN_TIMEOUT_SEC * 1000);

    // Wait for data to become available
    while (!httpClient.available() && millis() < timeout && httpClient.connected())
    {
      delay(10);
    }

    if (!httpClient.available())
    {
      DEBUGPRINT("ERROR: No response from server!");
      DEBUGPRINT("Connected: " + String(httpClient.connected()));
      DEBUGPRINT("Timeout reached");
      return false;
    }

    DEBUGPRINT("Data available, reading response...");

    // Problematic part! IP-API gives strange response sometimes, so we read it carefully
    // fixed below

    // Read character by character with strict timeout
    String fullResponse = "";
    uint32_t startRead = millis();
    uint32_t lastCharTime = millis();
    const uint32_t MAX_READ_TIME = 10000; // Maximum 10 seconds for complete read
    const uint32_t CHAR_TIMEOUT = 2000;   // Maximum 2 seconds between characters

    bool headersComplete = false;
    int contentLength = -1;

    while (millis() - startRead < MAX_READ_TIME)
    {
      if (httpClient.available())
      {
        char c = httpClient.read();
        fullResponse += c;
        lastCharTime = millis();

        // Check if headers are complete (but continue reading for body)
        if (!headersComplete && (fullResponse.indexOf("\r\n\r\n") != -1 || fullResponse.indexOf("\n\n") != -1))
        {
          headersComplete = true;

          // Extract Content-Length from headers
          int contentLengthPos = fullResponse.indexOf("Content-Length: ");
          if (contentLengthPos != -1)
          {
            int startPos = contentLengthPos + 16; // "Content-Length: " = 16 chars
            int endPos = fullResponse.indexOf('\r', startPos);
            if (endPos == -1)
              endPos = fullResponse.indexOf('\n', startPos);
            if (endPos != -1)
            {
              String lengthStr = fullResponse.substring(startPos, endPos);
              contentLength = lengthStr.toInt();
            }
          }
        }

        // If headers complete and we know content length, check if we have complete response
        if (headersComplete && contentLength > 0)
        {
          int headerSeparatorPos = fullResponse.indexOf("\r\n\r\n");
          if (headerSeparatorPos == -1)
            headerSeparatorPos = fullResponse.indexOf("\n\n");

          if (headerSeparatorPos != -1)
          {
            int headerLength = headerSeparatorPos + (fullResponse.indexOf("\r\n\r\n") != -1 ? 4 : 2);
            int bodyLength = fullResponse.length() - headerLength;

            if (bodyLength >= contentLength)
            {
              break;
            }
          }
        }

        // Prevent infinite response
        if (fullResponse.length() > 2048)
        {
          break;
        }
      }
      else if (!httpClient.connected())
      {
        break;
      }
      else if (millis() - lastCharTime > CHAR_TIMEOUT)
      {
        break;
      }
      else
      {
        delay(5); // Small delay between checks
      }
    }

    DEBUGPRINT("Read complete. Response length: " + String(fullResponse.length()));
    DEBUGPRINT("Read time: " + String(millis() - startRead) + "ms");

    if (fullResponse.length() == 0)
    {
      DEBUGPRINT("ERROR: No data received!");
      return false;
    }

    // Find JSON part (after HTTP headers separated by \r\n\r\n)
    int jsonStart = fullResponse.indexOf("\r\n\r\n");
    if (jsonStart != -1)
    {
      jsonStart += 4;
      DEBUGPRINT("Found JSON start after \\r\\n\\r\\n at position: " + String(jsonStart));
      _Response = fullResponse.substring(jsonStart);
      _Response.trim();
      DEBUGPRINT("Extracted JSON: " + _Response);
    }
    else
    {
      DEBUGPRINT("ERROR: Could not find JSON in response!");
      return false;
    }

    if (_Response.length() == 0)
    {
      DEBUGPRINT("ERROR: Empty JSON response!");
      return false;
    }

    JsonDocument doc;
    deserializeJson(doc, _Response);

    // Check if the request was successful
    String status = doc["status"].as<String>();
    if (status != "success")
    {
      DEBUGPRINT("ERROR: IP-API request failed with status: " + status);
      return false;
    }

    // Parse IP-API.com response format
    I->tz = doc["timezone"].as<String>();
    I->is_dst = doc["dst"];         // dst is boolean in IP-API
    I->offset = doc["offset"];      // offset is in seconds, convert to hours
    I->offset = I->offset / 3600.0; // Convert seconds to hours
    I->current_time = "";           // IP-API doesn't provide current time directly
    I->country = doc["country"].as<String>();
    I->country_code = doc["countryCode"].as<String>();
    I->city = doc["city"].as<String>();
    I->latitude = doc["lat"];
    I->longitude = doc["lon"];

    DEBUGPRINT(String("IP-API Current Time: Not provided by IP-API!"));
    DEBUGPRINT(String("IP-API Time Zone: ") + I->tz);
    DEBUGPRINT(String("IP-API DST: ") + (I->is_dst ? "true" : "false"));
    DEBUGPRINT(String("IP-API Offset: ") + I->offset);
    DEBUGPRINT(String("IP-API Country: ") + I->country);
    DEBUGPRINT(String("IP-API Country Code: ") + I->country_code);
    DEBUGPRINT(String("IP-API City: ") + I->city);
    DEBUGPRINT(String("IP-API Latitude: ") + I->latitude);
    DEBUGPRINT(String("IP-API Longitude: ") + I->longitude);

    return true;
  }
  return false;
}
