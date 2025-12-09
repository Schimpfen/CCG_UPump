// WebServerHandler.cpp --------------------------------
#include "WebServerHandler.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Wire.h>
#include "setpumpfrequency.h"

// externs from driver
extern bool bPumpState[3];
extern unsigned int nPumpVoltageByte[3];
extern unsigned int nFrequencyByte[3];
extern const unsigned int pumpChannels[3];

WebServerHandler::WebServerHandler(AsyncWebServer* srv) : server(srv) {}

void WebServerHandler::begin() {
  // Serve UI from SPIFFS /data
  server->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // Health
  server->on("/ping", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/plain", "pong");
  });

  // Scan mux+driver per pump
  server->on("/scan", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<512> doc;
    JsonArray out = doc.createNestedArray("pumps");

    for (uint8_t i=0;i<3;i++){
      tcaSelect(i);
      delay(2);
      Wire.beginTransmission(I2C_LOWDRIVER_ADRESS);
      uint8_t rc = Wire.endTransmission();

      JsonObject o = out.createNestedObject();
      o["id"]       = String((char)('A'+i));
      o["tca_ch"]   = pumpChannels[i];
      o["driverAck"]= (rc==0);
      o["on"]       = bPumpState[i];
      o["ampByte"]  = nPumpVoltageByte[i];
      o["freqByte"] = nFrequencyByte[i];
    }

    String s; serializeJson(doc, s);
    req->send(200, "application/json", s);
  });

  // Status snapshot
  server->on("/status", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<512> doc;
    JsonArray out = doc.createNestedArray("pumps");
    for (uint8_t i=0;i<3;i++){
      JsonObject o = out.createNestedObject();
      o["pump"]      = String((char)('A'+i));
      o["enabled"]   = bPumpState[i];
      o["amplitude"] = (AMP_V_MAX * (float)nPumpVoltageByte[i]) / 255.0f;        // Vpp
      o["frequency"] = 7.8125f * (float)(nFrequencyByte[i] ? nFrequencyByte[i] : 1); // Hz
    }
    String s; serializeJson(doc, s);
    req->send(200, "application/json", s);
  });

  // Stop all
  server->on("/stop", HTTP_POST, [](AsyncWebServerRequest* req){
    stopPumps();
    req->send(200, "text/plain", "stopped");
  });

  // Apply settings
  server->on("/apply", HTTP_POST,
    [](AsyncWebServerRequest* req){ /* onRequest */ },
    nullptr, // onUpload
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){ // onBody
      StaticJsonDocument<1024> doc;
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) { req->send(400, "text/plain", "invalid json"); return; }

      JsonArray arr = doc["pumps"].as<JsonArray>();
      if (arr.isNull()) { req->send(400, "text/plain", "missing pumps"); return; }

      for (JsonObject p : arr) {
        const char* id = p["pump"] | "";
        bool enabled   = p["enabled"] | true;
        unsigned vpp   = p["amplitude"] | 0;
        unsigned hz    = p["frequency"] | 100;

        uint8_t idx = 255;
        if (id && (id[0]=='A' || id[0]=='a')) idx = 0;
        else if (id && (id[0]=='B' || id[0]=='b')) idx = 1;
        else if (id && (id[0]=='C' || id[0]=='c')) idx = 2;

        if (idx > 2) continue;

        if (enabled) {
          Lowdriver_setvoltage(idx, vpp);
          Lowdriver_setfrequency(idx, hz);
          bPumpState[idx] = true;
        } else {
          Lowdriver_setvoltage(idx, 0);
          bPumpState[idx] = false;
        }
      }

      req->send(200, "text/plain", "ok");
    }
  );

  // Buzz quick test (sequential A->C)
  server->on("/buzz", HTTP_POST,
    [](AsyncWebServerRequest* req){ /* onRequest */ },
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
      StaticJsonDocument<256> doc;
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) { req->send(400, "text/plain", "invalid json"); return; }

      unsigned vpp = doc["vpp"] | 120;
      unsigned hz  = doc["hz"]  | 100;
      unsigned ms  = doc["ms"]  | 2000;

      for (uint8_t i=0;i<3;i++){
        Lowdriver_setvoltage(i, vpp);
        Lowdriver_setfrequency(i, hz);
        bPumpState[i] = true;
        delay(ms);
        Lowdriver_setvoltage(i, 0);
        bPumpState[i] = false;
        delay(150);
      }

      req->send(200, "text/plain", "buzz ok");
    }
  );

  // Start HTTP server
  server->begin();
}
