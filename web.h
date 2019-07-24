// web.h
#pragma once

// TODO: THIS IS NOT YET IMPLEMENTED

#if defined(__ESP__)

// Not yet available on ESP32!
#if !defined(__ESP32__)

#include <functional>

// ESP32: patch required currently: #if defined(ESP8266) || defined(ESP32)
#include <PubSubClient.h>

#include "platform.h"
#include "array.h"
#include "map.h"

#include "scheduler.h"

#include <ESP8266WebServer.h>

namespace ustd {
class Web {
  public:
    Scheduler *pSched;
    int tID;
    bool isOn = false;
    bool netUp = false;
    String webServer;
    ESP8266WebServer *pWebServer;

    Web() {
        webServer = "";
        isOn = false;
    }

    ~Web() {
        if (isOn) {
            isOn = false;
        }
    }

    void begin(Scheduler *_pSched) {
        // Make sure _clientName is Unique! Otherwise WEB server will rapidly
        // disconnect.
        pSched = _pSched;

        pWebServer = new ESP8266WebServer(80);

        // give a c++11 lambda as callback scheduler task registration of
        // this.loop():
        std::function<void()> ft = [=]() { this->loop(); };
        tID = pSched->add(ft, "web");

        std::function<void(String, String, String)> fnall =
            [=](String topic, String msg, String originator) {
                this->subsMsg(topic, msg, originator);
            };
        pSched->subscribe(tID, "#", fnall);

        pSched->publish("net/network/get");
        pSched->publish("net/services/webserver/get");
        isOn = true;
    }

    void loop() {
        if (isOn) {
        }
    }

    void subsMsg(String topic, String msg, String originator){};

};  // namespace ustd

}  // namespace ustd

#endif  // !defined(__ESP32__)
#endif  // defined(__ESP__)
