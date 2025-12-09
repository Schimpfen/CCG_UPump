//webserverhandler.h ----------------------------------------------//


#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class WebServerHandler {
public:
  explicit WebServerHandler(AsyncWebServer* srv);
  void begin();

private:
  AsyncWebServer* server;
};
