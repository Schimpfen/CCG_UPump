// main.cpp — single owner of: server, dnsServer, web, setup(), loop()
#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>

#include "setpumpfrequency.h"
#include "WebServerHandler.h"
#include "SerialHandler.h"

// I2C
static const int SDA_PIN = 21;
static const int SCL_PIN = 22;
static const uint32_t I2C_HZ = 100000;

// Wi-Fi / Web
const char* AP_SSID = "CCG_JustPump";
const char* AP_PSK  = "cellchipgroup";
const byte  DNS_PORT = 53;

AsyncWebServer server(80);            // defined ONLY here
WebServerHandler web(&server);        // defined ONLY here
DNSServer dnsServer;                  // defined ONLY here

// optional boot status log helper
extern bool bPumpState[3];
extern unsigned int nPumpVoltageByte[3];
extern unsigned int nFrequencyByte[3];
extern const unsigned int pumpChannels[3];

static void print_status_boot() {
  Serial.println("ID  On  Vpp    Hz     ampB freqB TCAch");
  for (uint8_t i=0;i<3;i++){
    float vpp = (AMP_V_MAX * (float)nPumpVoltageByte[i]) / 255.0f;
    float hz  = 7.8125f * (float)(nFrequencyByte[i] ? nFrequencyByte[i] : 1);
    Serial.printf("%c   %s  %6.1f %6.1f  %4u  %4u   %u\n",
      'A'+i, bPumpState[i]?"yes":"no", vpp, hz,
      nPumpVoltageByte[i], nFrequencyByte[i], pumpChannels[i]);
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis(); while(!Serial && (millis()-t0)<1200) {}
  Serial.println("\n[BOOT] ESP32 Pump Controller starting…");

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN, I2C_HZ);
  Wire.setTimeOut(50);
  Serial.printf("[I2C] SDA=%d SCL=%d @ %lu Hz\n", SDA_PIN, SCL_PIN, (unsigned long)I2C_HZ);

  // FS
  if (!SPIFFS.begin(true)) Serial.println("[FS] SPIFFS mount failed");
  else                     Serial.println("[FS] SPIFFS mounted");

  // Wi-Fi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PSK);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[AP] SSID=%s  IP=%s\n", AP_SSID, ip.toString().c_str());

  if (MDNS.begin("pump")) {
    MDNS.addService("http","tcp",80);
    Serial.println("[mDNS] http://pump.local");
  }
  dnsServer.start(DNS_PORT, "*", ip);

  // Web routes + static files
  web.begin();

  // Init pumps to safe idle
  Serial.println("[INIT] Initializing pumps A/B/C…");
  for (uint8_t i=0;i<3;i++) Lowdriver_init(i);
  print_status_boot();

  // Serial CLI
  SerialHandler_begin(SDA_PIN, SCL_PIN, I2C_HZ);
}

void loop() {
  dnsServer.processNextRequest();
  SerialHandler_poll();
}
