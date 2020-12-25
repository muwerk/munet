// net.h - the muwerk network

#pragma once

/*! \mainpage Munet a collection of network libraries for ESP8266 and ESP32
based on the cooperative scheduler muwerk.

\section Introduction

munet implements the classes:

* * \ref ustd::Net WLAN client connectivity and NTP time synchronization
* * \ref ustd::Ota Over-the-air (OTA) software update
* * \ref ustd::Mqtt connection to MQTT server

libraries are header-only and should work with any c++11 compiler
and support platforms esp8266 and esp32.

This library requires the libraries ustd, muwerk and requires a
<a href="https://github.com/muwerk/ustd/blob/master/README.md">platform
define</a>.

\section Reference
* * <a href="https://github.com/muwerk/munet">munet github repository</a>

depends on:
* * <a href="https://github.com/muwerk/ustd">ustd github repository</a>
* * <a href="https://github.com/muwerk/muwerk">muwerk github repository</a>

used by:
* * <a href="https://github.com/muwerk/mupplets">mupplets github repository</a>
*/

// #if defined(__ESP__)

#include "platform.h"
#include "array.h"
#include "map.h"

#include "scheduler.h"
#include "sensors.h"

#include <Arduino_JSON.h>  // Platformio lib no. 6249

namespace ustd {

/*! \brief Munet, the muwerk network class for WLAN and NTP

The library header-only and relies on the libraries ustd, muwerk, Arduino_JSON,
and PubSubClient.

Make sure to provide the <a
href="https://github.com/muwerk/ustd/blob/master/README.md">required platform
define</a> before including ustd headers.

See <a
href="https://github.com/muwerk/munet/blob/master/README.md">for network
credential and NTP configuration.</a>

Alternatively, credentials can be given in source code during Net.begin().
(s.b.)

## Sample network connection

~~~{.cpp}
#define __ESP__ 1   // Platform defines required, see doc, mainpage.
#include "scheduler.h"
#include "net.h"
#include "ota.h"  // optional for over-the-air software updates
#include "mqtt.h" // optional for connection to external MQTT server

ustd::Scheduler sched;
ustd::Net net(LED_BUILTIN);

ustd::Ota ota();  // optional
ustd::Mqtt mqtt();// optional

void appLoop();

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    net.begin(&sched);  // connect to WLAN and sync NTP time,
                        // credentials read from LittleFS, (net.json)
    ota.begin(&sched);  // optional ota update
    mqtt.begin(&sched); // optional connection to external MQTT server

    int tID = sched.add(appLoop, "main");  // create task for your app code
}

void appLoop() {
    // your code goes here.
}

// Never add code to this loop, use appLoop() instead.
void loop() {
    sched.loop();
}
~~~
 */

class Net {
  public:
    enum Netstate { NOTDEFINED, NOTCONFIGURED, CONNECTINGAP, CONNECTED };
    enum Netmode { AP, STATION };

    unsigned int reconnectMaxRetries = 40;
    unsigned int wifiConnectTimeout = 15;
    bool bRebootOnContinuedWifiFailure = true;

  private:
    Netstate state;
    Netstate oldState;
    Netmode mode;
    long conTime;
    uint8_t signalLed;
    unsigned long conTimeout = 15000;
    String SSID;
    String password;
    String localHostname;
    String ipAddress;
    Scheduler *pSched;
    int tID;
    unsigned long tick1sec;
    unsigned long tick10sec;
    ustd::sensorprocessor rssival = ustd::sensorprocessor(20, 1800, 2.0);
    ustd::map<String, String> netServices;
    String macAddress;
    bool bOnceConnected = false;
    int deathCounter;
    int initialCounter;

  public:
    Net(uint8_t signalLed = 0xff) : signalLed(signalLed) {
        /*! Instantiate a network object for WLAN and NTP connectivity.
         *
         * The Net object publishes messages using muwerk's pub/sub intertask
         * communication (which does not rely on MQTT servers), other muwerk
         * tasks can subscribe to the following topics:
         *
         * subscribe('net/network'); for information about wlan connection state
         * changes. Status can be actively requested by
         * publish('net/network/get');
         *
         * subscribe('net/rssi'); for information about WLAN reception strength
         *
         * subscribe('net/services'); for a list of available network services.
         *
         * subscribe('net/networks'); for a list of WLANs nearby.
         *
         * @param signalLed (optional), Pin that will be set to LOW (led on)
         * during network connection attempts. Once connected, led is switched
         * off and can be used for other functions. Led on signals that the ESP
         * is trying to connect to a network.
         */
        oldState = NOTDEFINED;
        state = NOTCONFIGURED;
        if (signalLed != 0xff) {
            pinMode(signalLed, OUTPUT);
            digitalWrite(signalLed, HIGH);  // Turn the LED off
        }
    }

    void configureNTP() {
        if (netServices.find("timeserver") != -1) {
#define RTC_TEST 0  // = put a time_t from RTC in here...

            timeval tv = {RTC_TEST, 0};
            timezone tz = {0, 0};
            settimeofday(&tv, &tz);
            configTime(0, 0, netServices["timeserver"].c_str());
            if (netServices.find("dstrules") != -1) {
                String dstrules = netServices["dstrules"];
                setenv("TZ", dstrules.c_str(), 3);
                tzset();
            }
        }
    }

    void begin(Scheduler *_pSched, bool _restartEspOnRepeatedFailure = true, String _ssid = "",
               String _password = "", Netmode _mode = AP) {
        /*! Connect to WLAN network and request NTP time
         *
         * This function starts the connection to a WLAN network, by default
         * using the network credentials configured in net.json. Once a
         * connection is established, a NTP time server is contacted and time is
         * set according to local timezone rules as configured in net.json.
         *
         * Note: NTP configuration is only available via net.json, ssid and
         * password can also be set using this function.
         *
         * Other muwerk task can subscribe to topic 'net/network' to receive
         * information about network connection states.
         *
         * @param _pSched Pointer to the muwerk scheduler.
         * @param _restartEspOnRepeatedFailure (optional, default true) restarts
         * ESP on continued failure.
         * @param _ssid (optional, default unused) Alternative way to specify
         * WLAN SSID not using net.json.
         * @param _password (optional, default unused) Alternative way to
         * specify WLAN password.
         * @param _mode (optional, default AP) Currently unused network mode.
         */
        pSched = _pSched;

        deathCounter = reconnectMaxRetries;
        initialCounter = reconnectMaxRetries;

        bRebootOnContinuedWifiFailure = _restartEspOnRepeatedFailure;

        SSID = _ssid;
        password = _password;
        mode = _mode;
        tick1sec = millis();
        tick10sec = millis();
        bool directInit = false;

        if (_ssid != "")
            directInit = true;

        readNetConfig();

        if (directInit) {
            SSID = _ssid;
            password = _password;
#if defined(__ESP32__)
            localHostname = WiFi.getHostname();
#else
            localHostname = WiFi.hostname();
#endif
        }
        connectAP();

        // give a c++11 lambda as callback scheduler task registration of
        // this.loop():
        std::function<void()> ft = [=]() { this->loop(); };
        tID = pSched->add(ft, "net");

        // give a c++11 lambda as callback for incoming mqttmessages:
        std::function<void(String, String, String)> fng = [=](String topic, String msg,
                                                              String originator) {
            this->subsNetGet(topic, msg, originator);
        };
        pSched->subscribe(tID, "net/network/get", fng);
        std::function<void(String, String, String)> fns = [=](String topic, String msg,
                                                              String originator) {
            this->subsNetSet(topic, msg, originator);
        };
        pSched->subscribe(tID, "net/network/set", fns);
        std::function<void(String, String, String)> fnsg = [=](String topic, String msg,
                                                               String originator) {
            this->subsNetsGet(topic, msg, originator);
        };
        pSched->subscribe(tID, "net/networks/get", fnsg);
        std::function<void(String, String, String)> fsg = [=](String topic, String msg,
                                                              String originator) {
            this->subsNetServicesGet(topic, msg, originator);
        };
        pSched->subscribe(tID, "net/services/+/get", fsg);
    }

  private:
    void publishNetwork() {
        String json;
        if (mode == AP) {
            json = "{\"mode\":\"ap\",";
        } else if (mode == STATION) {
            json = "{\"mode\":\"station\",";
        } else {
            json = "{\"mode\":\"undefined\",";
        }
        json += "\"mac\":\"" + macAddress + "\",";
        switch (state) {
        case NOTCONFIGURED:
            json += "\"state\":\"notconfigured\"}";
            break;
        case CONNECTINGAP:
            json += "\"state\":\"connectingap\",\"SSID\":\"" + SSID + "\"}";
            break;
        case CONNECTED:
            json += "\"state\":\"connected\",\"SSID\":\"" + SSID + "\",\"hostname\":\"" +
                    localHostname + "\",\"ip\":\"" + ipAddress + "\"}";
            break;
        default:
            json += "\"state\":\"undefined\"}";
            break;
        }
        pSched->publish("net/network", json);
#ifdef USE_SERIAL_DBG
        Serial.println("Net: published net/network");
#endif
        if (state == CONNECTED) {
            publishServices();
#ifdef USE_SERIAL_DBG
            Serial.println("Net: published services");
#endif
        }
    }

    bool readNetConfig() {
#ifdef USE_SERIAL_DBG
        Serial.println("Reading net.json");
#endif
#ifdef __USE_OLD_FS__
        SPIFFS.begin();
        fs::File f = SPIFFS.open("/net.json", "r");
#ifdef USE_SERIAL_DBG
        Serial.println("Reading net.json via SPIFFS");
#endif
#else
        LittleFS.begin();
        fs::File f = LittleFS.open("/net.json", "r");
#ifdef USE_SERIAL_DBG
        Serial.println("Reading net.json via LittleFS");
#endif
#endif
        if (!f) {
#ifdef USE_SERIAL_DBG
            Serial.println("Failed to open /net.json");
#endif
            return false;
        } else {
            String jsonstr = "";
            while (f.available()) {
                // Lets read line by line from the file
                String lin = f.readStringUntil('\n');
                jsonstr = jsonstr + lin;
            }
            f.close();
            JSONVar configObj = JSON.parse(jsonstr);
            if (JSON.typeof(configObj) == "undefined") {
#ifdef USE_SERIAL_DBG
                Serial.println("publishNetworks, config data: Parsing input failed!");
                Serial.println(jsonstr);
#endif
                return false;
            }
            SSID = (const char *)configObj["SSID"];
            password = (const char *)configObj["password"];
            localHostname = (const char *)configObj["hostname"];

            if (configObj.hasOwnProperty("services")) {
#ifdef USE_SERIAL_DBG
                Serial.println("Net: Found services config");
#endif
                JSONVar arr = configObj["services"];
                for (int i = 0; i < arr.length(); i++) {
                    JSONVar dc = arr[i];
                    JSONVar keys = dc.keys();
                    for (int j = 0; j < keys.length(); j++) {
                        netServices[(const char *)keys[j]] = (const char *)dc[keys[j]];
#ifdef USE_SERIAL_DBG
                        Serial.println((const char *)keys[j]);
#endif
                    }
                }
            } else {
#ifdef USE_SERIAL_DBG
                Serial.println("Net: no services configured, that is probably unexpected!");
#endif
            }
            return true;
        }
    }

    void connectAP() {
#ifdef USE_SERIAL_DBG
        Serial.println("Connect-AP");
        Serial.println(SSID.c_str());
#endif
        WiFi.mode(WIFI_STA);
        WiFi.begin(SSID.c_str(), password.c_str());
        macAddress = WiFi.macAddress();

        if (localHostname != "") {
#if defined(__ESP32__)
            WiFi.setHostname(localHostname.c_str());
#else
            WiFi.hostname(localHostname.c_str());
#endif
        } else {
#if defined(__ESP32__)
            localHostname = WiFi.getHostname();
#else
            localHostname = WiFi.hostname();
#endif
        }
        state = CONNECTINGAP;
        conTime = millis();
    }

    String strEncryptionType(int thisType) {
        // read the encryption type and print out the name:
#if !defined(__ESP32__)
        switch (thisType) {
        case ENC_TYPE_WEP:
            return "WEP";
            break;
        case ENC_TYPE_TKIP:
            return "WPA";
            break;
        case ENC_TYPE_CCMP:
            return "WPA2";
            break;
        case ENC_TYPE_NONE:
            return "None";
            break;
        case ENC_TYPE_AUTO:
            return "Auto";
            break;
        default:
            return "unknown";
            break;
        }
#else
        switch (thisType) {
        case WIFI_AUTH_OPEN:
            return "None";
            break;
        case WIFI_AUTH_WEP:
            return "WEP";
            break;
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
            break;
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA_WPA2_PSK";
            break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2_ENTERPRISE";
            break;
        default:
            return "unknown";
            break;
        }
#endif
    }

    void publishNetworks() {
        int numSsid = WiFi.scanNetworks();
        if (numSsid == -1) {
            pSched->publish("net/networks",
                            "{}");  // "{\"state\":\"error\"}");
            return;
        }
        String netlist = "{";
        for (int thisNet = 0; thisNet < numSsid; thisNet++) {
            if (thisNet > 0)
                netlist += ",";
            netlist += "\"" + WiFi.SSID(thisNet) + "\":{\"rssi\":" + String(WiFi.RSSI(thisNet)) +
                       ",\"enc\":\"" + strEncryptionType(WiFi.encryptionType(thisNet)) + "\"}";
        }
        netlist += "}";
        pSched->publish("net/networks", netlist);
    }

    void publishServices() {
        for (unsigned int i = 0; i < netServices.length(); i++) {
            pSched->publish("net/services/" + netServices.keys[i],
                            "{\"server\":\"" + netServices.values[i] + "\"}");
        }
    }

    void subsNetGet(String topic, String msg, String originator) {
        publishNetwork();
    }
    void subsNetsGet(String topic, String msg, String originator) {
        publishNetworks();
    }
    void subsNetSet(String topic, String msg, String originator) {
        // XXX: not yet implemented.
    }

    void subsNetServicesGet(String topic, String msg, String originator) {
        for (unsigned int i = 0; i < netServices.length(); i++) {
            if (topic == "net/services/" + netServices.keys[i] + "/get") {
                pSched->publish("net/services/" + netServices.keys[i],
                                "{\"server\":\"" + netServices.values[i] + "\"}");
            }
        }
    }

    void loop() {
        switch (state) {
        case NOTCONFIGURED:
            if (timeDiff(tick10sec, millis()) > 10000) {
                tick10sec = millis();
                publishNetworks();
            }
            break;
        case CONNECTINGAP:
            if (WiFi.status() == WL_CONNECTED) {
#ifdef USE_SERIAL_DBG
                Serial.println("Connected!");
#endif
                state = CONNECTED;
                IPAddress ip = WiFi.localIP();
                ipAddress =
                    String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
                configureNTP();
            } else {
                if (ustd::timeDiff(conTime, millis()) > conTimeout) {
#ifdef USE_SERIAL_DBG
                    Serial.println("Timeout connecting!");
#endif
                    if (bOnceConnected) {
                        if (bRebootOnContinuedWifiFailure)
                            --deathCounter;
                        if (deathCounter == 0) {
#ifdef USE_SERIAL_DBG
                            Serial.println("Final failure, restarting...");
#endif
                            if (bRebootOnContinuedWifiFailure)
                                ESP.restart();
                        }
#ifdef USE_SERIAL_DBG
                        Serial.println("reconnecting...");
#endif
                        WiFi.reconnect();
                        conTime = millis();
                    } else {
#ifdef USE_SERIAL_DBG
                        Serial.println("retrying to connect...");
#endif
                        if (initialCounter > 0) {
                            if (bRebootOnContinuedWifiFailure)
                                --initialCounter;
                            WiFi.reconnect();
                            conTime = millis();
                            state = CONNECTINGAP;

                        } else {
#ifdef USE_SERIAL_DBG
                            Serial.println("Final connect failure, "
                                           "configuration invalid?");
#endif
                            state = NOTCONFIGURED;
                            if (bRebootOnContinuedWifiFailure)
                                ESP.restart();
                        }
                    }
                }
            }
            break;
        case CONNECTED:
            bOnceConnected = true;
            deathCounter = reconnectMaxRetries;

            if (timeDiff(tick1sec, millis()) > 1000) {
                tick1sec = millis();
                if (WiFi.status() == WL_CONNECTED) {
                    long rssi = WiFi.RSSI();
                    if (rssival.filter(&rssi)) {
                        pSched->publish("net/rssi", "{\"rssi\":" + String(rssi) + "}");
                    }
                } else {
                    WiFi.reconnect();
                    state = CONNECTINGAP;
                    conTime = millis();
                }
            }
            break;
        default:
            break;
        }
        if (state != oldState) {
#ifdef USE_SERIAL_DBG
            char msg[128];
            sprintf(msg, "Netstate: %d->%d", oldState, state);
            Serial.println(msg);
            if (state == 3) {  // connected!
                Serial.print("RSSI: ");
                Serial.println(WiFi.RSSI());
            }
#endif
            if (state == NOTCONFIGURED || state == CONNECTED) {
                if (signalLed != 0xff) {
                    digitalWrite(signalLed, HIGH);  // Turn the LED off
                }
            } else {
                if (signalLed != 0xff) {
                    digitalWrite(signalLed, LOW);  // Turn the LED on
                }
            }
            oldState = state;
            publishNetwork();
        }
    }
};  // namespace ustd
}  // namespace ustd

// #endif  // defined(__ESP__)
