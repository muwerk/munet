// ota.h
#pragma once

#if defined(__ESP__)

#include <functional>

#include "platform.h"
#include "array.h"
#include "map.h"

#include "scheduler.h"

#include <ArduinoOTA.h>

namespace ustd {
class Ota {
  public:
    Scheduler *pSched;
    int tID;
    String hostName;
    bool bOTAUpdateActive = false;
    bool bNetUp = false;
    bool bCheckOTA = false;

    Ota() {
    }

    ~Ota() {
    }

    void OTAsetup() {
        ArduinoOTA.setHostname(hostName.c_str());

        // No authentication by default
        // ArduinoOTA.setPassword("chopon");

        // Password can be set with it's md5 value as well
        // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
        // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

        ArduinoOTA.onStart([&]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
                type = "sketch";
            else  // U_SPIFFS
                type = "filesystem";

// NOTE: if updating SPIFFS this would be the place to unmount
// SPIFFS using SPIFFS.end()
#ifdef USE_SERIAL_DBG
            Serial.print("Start updating ");
            Serial.println(type.c_str());
#endif
            bOTAUpdateActive = true;
            pSched->singleTaskMode(tID);
        });
        ArduinoOTA.onEnd([&]() {
#ifdef USE_SERIAL_DBG
            Serial.println("\nEnd");
#endif
            pSched->singleTaskMode(-1);
            bOTAUpdateActive = false;
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
#ifdef USE_SERIAL_DBG
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
#endif
        });
        ArduinoOTA.onError([&](ota_error_t error) {
#ifdef USE_SERIAL_DBG
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR)
                Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR)
                Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR)
                Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR)
                Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR)
                Serial.println("End Failed");
#endif
        });
        ArduinoOTA.begin();
    }

    void begin(Scheduler *_pSched) {
        pSched = _pSched;
#if defined(__ESP32__)
        hostName = WiFi.getHostname();
#else
        hostName = WiFi.hostname();
#endif

        // give a c++11 lambda as callback scheduler task registration of
        // this.loop():
        std::function<void()> ft = [=]() { this->loop(); };
        tID = pSched->add(ft, "ota", 25000L);  // check for ota every 25ms

        std::function<void(String, String, String)> fnall =
            [=](String topic, String msg, String originator) {
                this->subsMsg(topic, msg, originator);
            };
        pSched->subscribe(tID, "#", fnall);

        pSched->publish("net/network/get");
    }

    void loop() {
        if (bCheckOTA) {
            ArduinoOTA.handle();
        }
    }

    void subsMsg(String topic, String msg, String originator) {
        DynamicJsonBuffer jsonBuffer(200);
        JsonObject &root = jsonBuffer.parseObject(msg);
        if (!root.success()) {
            // DBG("mqtt: Invalid JSON received: " + String(msg));
            return;
        }

        if (topic == "net/network") {
            String state = root["state"];
            if (state == "connected") {
                if (!bNetUp) {
                    bNetUp = true;
                    OTAsetup();
                    bCheckOTA = true;
                }
            } else {
                bNetUp = false;
                bCheckOTA = false;
            }
        }
    };
};

}  // namespace ustd

#endif  // defined(__ESP__)
