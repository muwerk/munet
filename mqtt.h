// MQTT.h

#pragma once

#if defined(__ESP__)

#include <functional>

// ESP32: patch required currently: #if defined(ESP8266) || defined(ESP32)
#include <ArduinoJson.h>
#include <PubSubClient.h>

#include "platform.h"
#include "array.h"
#include "map.h"

#include "scheduler.h"

namespace ustd {
class Mqtt {
  public:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    bool bMqInit = false;
    Scheduler *pSched;
    String domainToken = "mu";
    int tID;

    bool isOn = false;
    bool netUp = false;
    bool mqttConnected = false;
    bool bCheckConnection = false;
    unsigned long mqttTicker;
    unsigned long mqttTickerTimeout = 5000L;
    String clientName;
    String mqttServer;
    IPAddress mqttserverIP;

    Mqtt() {
        mqttServer = "";
        isOn = false;
    }

    ~Mqtt() {
        if (isOn) {
            isOn = false;
        }
    }

    void begin(Scheduler *_pSched, String _clientName = "") {
        // Make sure _clientName is Unique! Otherwise MQTT server will rapidly
        // disconnect.
        pSched = _pSched;
        clientName = _clientName;
        mqttClient = wifiClient;

        mqttTicker = millis();
        if (clientName == "") {
#if defined(__ESP32__)
            clientName = WiFi.getHostname();
#else
            clientName = String(WiFi.hostname().c_str());
#endif
        }

        // give a c++11 lambda as callback scheduler task registration of
        // this.loop():
        std::function<void()> ft = [=]() { this->loop(); };
        tID = pSched->add(ft, "mqtt");

        std::function<void(String, String, String)> fnall =
            [=](String topic, String msg, String originator) {
                this->subsMsg(topic, msg, originator);
            };
        pSched->subscribe(tID, "#", fnall);

        pSched->publish("net/network/get");
        pSched->publish("net/services/mqttserver/get");
        isOn = true;
    }

    bool bWarned = false;
    void loop() {
        if (isOn) {
            if (netUp && mqttServer != "") {
                if (mqttConnected) {
                    mqttClient.loop();
                }

                if (bCheckConnection ||
                    timeDiff(mqttTicker, millis()) > mqttTickerTimeout) {
                    mqttTicker = millis();
                    bCheckConnection = false;
                    if (!mqttClient.connected()) {
                        // Attempt to connect
                        if (mqttClient.connect(clientName.c_str())) {
                            mqttConnected = true;
                            mqttClient.subscribe((clientName + "/#").c_str());
                            mqttClient.subscribe((domainToken + "/#").c_str());
                            bWarned = false;
                        } else {
                            if (!bWarned) {
                                bWarned = true;
                            }
                            mqttConnected = false;
                        }
                    }
                }
            }
        }
    }

    void mqttReceive(char *ctopic, unsigned char *payload,
                     unsigned int length) {
        String msg;
        String topic;
        String tokn;
        ustd::array<String> toks;
        msg = "";
        for (unsigned int i = 0; i < length; i++) {
            msg += (char)payload[i];
        }
        toks.add(clientName);
        String genTok = domainToken;
        toks.add(genTok);
        for (unsigned int i = 0; i < toks.length(); i++) {
            if (strlen(ctopic) > toks[i].length()) {
                tokn = toks[i] + '/';
                if (!strncmp(ctopic, tokn.c_str(), tokn.length())) {
                    topic = (const char *)(&ctopic[tokn.length()]);
                    pSched->publish(topic, msg, "mqtt");
                }
            }
        }
    }

    void subsMsg(String topic, String msg, String originator) {
        if (originator == "mqtt")
            return;  // avoid loops
        if (mqttConnected) {
            unsigned int len = msg.length() + 1;
            String tpc = clientName + "/" + topic;
            if (mqttClient.publish(tpc.c_str(), msg.c_str(), len)) {
#ifdef USE_SERIAL_DBG
                Serial.println(
                    ("MQTT publish: " + topic + " | " + String(msg)).c_str());
#endif
            } else {
#ifdef USE_SERIAL_DBG
                Serial.println(("MQTT ERROR len=" + String(len) +
                                ", not published: " + topic + " | " +
                                String(msg))
                                   .c_str());
#endif
                if (len > 128) {
#ifdef USE_SERIAL_DBG
                    Serial.println("FATAL ERROR: you need to re-compile the "
                                   "PubSubClient library and increase #define "
                                   "MQTT_MAX_PACKET_SIZE.");
#endif
                }
            }
        } else {
#ifdef USE_SERIAL_DBG
            Serial.println(("MQTT can't publish, MQTT down: " + topic).c_str());
#endif
        }
        DynamicJsonBuffer jsonBuffer(200);
        JsonObject &root = jsonBuffer.parseObject(msg);
        if (!root.success()) {
#ifdef USE_SERIAL_DBG
            Serial.println(
                ("mqtt: Invalid JSON received: " + String(msg)).c_str());
#endif
            return;
        }
        if (topic == "net/services/mqttserver") {
            if (!bMqInit) {
                mqttServer = root["server"].as<char *>();
                bCheckConnection = true;
                mqttClient.setServer(mqttServer.c_str(), 1883);
                // give a c++11 lambda as callback for incoming mqtt
                // messages:
                std::function<void(char *, unsigned char *, unsigned int)> f =
                    [=](char *t, unsigned char *m, unsigned int l) {
                        this->mqttReceive(t, m, l);
                    };
                // If this breaks for ESP32, patch pubsubclient, l.76:
                // #if defined(ESP8266) or defined(ESP32)
                // to allow functionals for callback signature
                mqttClient.setCallback(f);
                bMqInit = true;
            }
        }
        if (topic == "net/network") {
            String state = root["state"];
            if (state == "connected") {
                if (!netUp) {
                    netUp = true;
                    bCheckConnection = true;
                }
            } else {
                netUp = false;
            }
        }
    };
};

}  // namespace ustd

#endif  // defined(__ESP__)
