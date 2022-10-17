// web.h
#pragma once

#if defined(__ESP__)

#include "ustd_platform.h"

#if defined(__ESP32__) || defined(__ESP32_RISC__)
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#else
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#endif

#include "ustd_array.h"
#include "ustd_map.h"
#include "scheduler.h"

namespace ustd {
class Web {
  public:
    Scheduler *pSched;
    int tID;
    bool netUp = false;
    bool webUp = false;
    String webServer;
#if defined(__ESP32__) || defined(__ESP32_RISC__)
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
#ifdef __USE_SPIFFS_FS__
        SPIFFS.begin();
#else
        LittleFS.begin();
#endif

#if defined(__ESP32__) || defined(__ESP32_RISC__)
        pWebServer = new WebServer(80);
#else
        pWebServer = new ESP8266WebServer(80);
#endif
        netUp = false;

        auto ft = [=]() { this->loop(); };
        tID = pSched->add(ft, "web");

        auto fnall = [=](String topic, String msg, String originator) {
            this->subsMsg(topic, msg, originator);
        };
        pSched->subscribe(tID, "net/network", fnall);

        pSched->publish("net/network/get");
        // pSched->publish("net/services/webserver/get");
    }

    void handleRoot() {
        handleFileSystem();
    }
    String getContentType(String fileName) {
        if (fileName.endsWith(".html"))
            return "text/html";
        if (fileName.endsWith(".css"))
            return "text/css";
        if (fileName.endsWith(".png"))
            return "image/png";
        if (fileName.endsWith(".js"))
            return "application/javascript";
        if (fileName.endsWith(".ico"))
            return "image/x-icon";
        return "text/plain";
    }

    void handleFileSystem() {
        String fileName = pWebServer->uri();
        if (fileName == "/")
            fileName = "/index.html";
        String contentType = getContentType(fileName);
#ifdef __USE_SPIFFS_FS__
        if (SPIFFS.exists(fileName)) {                // If the file exists
            fs::File f = SPIFFS.open(fileName, "r");  // Open it
#else
        if (LittleFS.exists(fileName)) {                // If the file exists
            fs::File f = LittleFS.open(fileName, "r");  // Open it
#endif
            /*size_t sent = */ pWebServer->streamFile(f, contentType);  // And send it to the client
            f.close();                                                  // Then close the file again
            return;
        } else {
            handleNotFound();
        }
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

        pWebServer->on("/inline",
                       [&]() { pWebServer->send(200, "text/plain", "this works as well"); });

        pWebServer->on("/result", [&]() {
            String ssid = pWebServer->arg("ssid");
            String hostname = pWebServer->arg("hostname");
            String response = "{\"ssid\": \"" + ssid + "\", \"hostname\": \"" + hostname + "\"}";
            pWebServer->send(200, "text/plain", response.c_str());
            pSched->publish("webserver/data", response);
        });

        auto fnf = [=]() { this->handleFileSystem(); };
        pWebServer->onNotFound(fnf);
    }

    void loop() {
        if (netUp) {
            pWebServer->handleClient();
#if !defined(__ESP32__) && !defined(__ESP32_RISC__)
            MDNS.update();
#endif
        }
    }

    void subsMsg(String topic, String msg, String originator) {

        if (topic == "net/network") {
            JSONVar jsonMsg = JSON.parse(msg);
            String state = (const char *)jsonMsg["state"];  // root["state"];
            if (state == "connected") {
                if (!webUp) {
                    netUp = true;
                    MDNS.begin("esp8266");
                    pWebServer->begin();
                    initHandles();
                    webUp = true;
                }
            } else {
                netUp = false;
            }
        }
    }
};  // Web

}  // namespace ustd

#endif  // defined(__ESP__)
