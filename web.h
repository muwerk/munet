// web.h
#pragma once

//#if defined(__ESP__)

#include "../ustd/platform.h"

#ifdef __ESP32__
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#else
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#endif

#include "array.h"
#include "map.h"
#include "scheduler.h"

namespace ustd {
class Web {
  public:
    Scheduler *pSched;
    int tID;
    bool netUp = false;
    bool webUp = false;
    String webServer;
    #ifdef __ESP32__
    WebServer *pWebServer;
    #else
    ESP8266WebServer *pWebServer;
    #endif

    Web() {
        webServer = "";
    }

    ~Web() {
    }

    void begin(Scheduler *_pSched) {
        pSched = _pSched;

        #ifdef __ESP32__
        pWebServer = new WebServer(80);
        #else
        pWebServer = new ESP8266WebServer(80);
        #endif
        netUp=false;

        auto ft = [=]() { this->loop(); };
        tID = pSched->add(ft, "web");

        auto fnall = [=](String topic, String msg, String originator) {
                this->subsMsg(topic, msg, originator);
            };
        pSched->subscribe(tID, "net/network", fnall);

        pSched->publish("net/network/get");
        //pSched->publish("net/services/webserver/get");
    }

    void handleRoot() {
        pWebServer->send(200, "text/plain", "hello from esp8266!");
    }

    void handleNotFound() {
        String message = "File Not Found\n\n";
        message += "URI: ";
        message += pWebServer->uri();
        message += "\nMethod: ";
        message += (pWebServer->method() == HTTP_GET) ? "GET" : "POST";
        message += "\nArguments: ";
        message += pWebServer->args();
        message += "\n";
        for (uint8_t i = 0; i < pWebServer->args(); i++) {
            message += " " + pWebServer->argName(i) + ": " + pWebServer->arg(i) + "\n";
        }
        pWebServer->send(404, "text/plain", message);
    }


    void initHandles() {
        auto frt = [=]() { this->handleRoot(); };
        pWebServer->on("/", frt);

        pWebServer->on("/inline", [&]() {
            pWebServer->send(200, "text/plain", "this works as well");
        });
    }
/*
        pWebServer->on("/gif", []() {
            static const uint8_t gif[] PROGMEM = {
            0x47, 0x49, 0x46, 0x38, 0x37, 0x61, 0x10, 0x00, 0x10, 0x00, 0x80, 0x01,
            0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x2c, 0x00, 0x00, 0x00, 0x00,
            0x10, 0x00, 0x10, 0x00, 0x00, 0x02, 0x19, 0x8c, 0x8f, 0xa9, 0xcb, 0x9d,
            0x00, 0x5f, 0x74, 0xb4, 0x56, 0xb0, 0xb0, 0xd2, 0xf2, 0x35, 0x1e, 0x4c,
            0x0c, 0x24, 0x5a, 0xe6, 0x89, 0xa6, 0x4d, 0x01, 0x00, 0x3b
            };
            char gif_colored[sizeof(gif)];
            memcpy_P(gif_colored, gif, sizeof(gif));
            // Set the background to a random set of colors
            gif_colored[16] = millis() % 256;
            gif_colored[17] = millis() % 256;
            gif_colored[18] = millis() % 256;
            pWebServer->send(200, "image/gif", gif_colored, sizeof(gif_colored));
        });

        pWebServer->onNotFound(handleNotFound);
    }
*/
   void loop() {
        if (netUp) {
            pWebServer->handleClient();
            #ifndef __ESP32__
            MDNS.update();
            #endif
        }
    }

    void subsMsg(String topic, String msg, String originator) {

        if (topic=="net/network") {
            JSONVar jsonMsg = JSON.parse(msg);
            String state = (const char *)jsonMsg["state"];  // root["state"];
            if (state == "connected") {
                if (!webUp) {
                    netUp = true;
                    MDNS.begin("esp8266");
                    pWebServer->begin();
                    initHandles();
                    webUp=true;
                }
            } else {
                netUp = false;
            }   
        }
    }
};  // Web

}  // namespace ustd

//#endif  // defined(__ESP__)
