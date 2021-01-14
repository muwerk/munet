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
#include "jsonfile.h"
#include "metronome.h"

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
    const long NET_CONFIG_VERSION = 1;
    enum Netstate { NOTDEFINED, NOTCONFIGURED, SERVING, CONNECTINGAP, CONNECTED };
    enum Netmode { OFF, AP, STATION, BOTH };

  private:
    // hardware configuration
    uint8_t signalLed;
    bool signalLogic;

    // active configuration
    JsonFile config;

    // cached configuration values
    unsigned int reconnectMaxRetries = 40;
    unsigned long connectTimeout = 15000;
    bool bRebootOnContinuedWifiFailure = true;

    // hardware info
    String apmAddress;
    String macAddress;
    String hexAddress;
    String deviceID;

    // runtime control - state management
    Netmode mode;
    Netstate curState;
    Netstate oldState;
    ustd::metronome statePublisher = 30000;
    // runtime control - station connection management
    ustd::metronome connectionMonitor = 1000;
    long conTime;
    bool bOnceConnected;
    int initialCounter;
    int deathCounter;
    // runtime control - wifi scanning
    bool scanning = false;

    // operating values - station
    ustd::sensorprocessor rssival = ustd::sensorprocessor(20, 1800, 2.0);
    // operating values - ap
    unsigned int connections;

    // muwerk task management
    Scheduler *pSched;
    int tID;

  public:
    Net(uint8_t signalLed = 0xff, bool signalLogic = false)
        : signalLed(signalLed), signalLogic(signalLogic) {
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
        curState = NOTCONFIGURED;
        if (signalLed != 0xff) {
            pinMode(signalLed, OUTPUT);
            setLed(signalLogic);  // Turn the LED off
        }
    }

    void begin(Scheduler *_pSched) {
        updateMacAddr();
        readNetConfig();

        pSched = _pSched;
        tID = pSched->add([this]() { this->loop(); }, "net");

        pSched->subscribe(
            tID, "net/network/get",
            [this](String topic, String msg, String originator) { this->publishState(); });

        pSched->subscribe(
            tID, "net/networks/get",
            [this](String topic, String msg, String originator) { this->requestScan(msg); });

        startServices();
    }

    // void xxxbegin(Scheduler *_pSched, bool _restartEspOnRepeatedFailure = true, String _ssid =
    // "",
    //               String _password = "", Netmode _mode = AP) {
    //     /*! Connect to WLAN network and request NTP time
    //      *
    //      * This function starts the connection to a WLAN network, by default
    //      * using the network credentials configured in net.json. Once a
    //      * connection is established, a NTP time server is contacted and time is
    //      * set according to local timezone rules as configured in net.json.
    //      *
    //      * Note: NTP configuration is only available via net.json, ssid and
    //      * password can also be set using this function.
    //      *
    //      * Other muwerk task can subscribe to topic 'net/network' to receive
    //      * information about network connection states.
    //      *
    //      * @param _pSched Pointer to the muwerk scheduler.
    //      * @param _restartEspOnRepeatedFailure (optional, default true) restarts
    //      * ESP on continued failure.
    //      * @param _ssid (optional, default unused) Alternative way to specify
    //      * WLAN SSID not using net.json.
    //      * @param _password (optional, default unused) Alternative way to
    //      * specify WLAN password.
    //      * @param _mode (optional, default AP) Currently unused network mode.
    //      */
    //     updateMacAddr();
    //     pSched = _pSched;
    //     bRebootOnContinuedWifiFailure = _restartEspOnRepeatedFailure;

    //     SSID = _ssid;
    //     password = _password;
    //     mode = _mode;
    //     tick1sec = millis();
    //     tick10sec = millis();
    //     bool directInit = false;

    //     if (_ssid != "")
    //         directInit = true;

    //     readNetConfig();

    //     if (directInit) {
    //         SSID = _ssid;
    //         password = _password;
    //     }

    //     if (mode == AP) {
    //         startAP();
    //     } else {
    //         startSTATION();
    //     }

    //     tID = pSched->add([this]() { this->loop(); }, "net");

    //     pSched->subscribe(
    //         tID, "net/network/get",
    //         [this](String topic, String msg, String originator) { this->publishState(); });

    //     pSched->subscribe(
    //         tID, "net/networks/get",
    //         [this](String topic, String msg, String originator) { this->requestScan(msg); });

    //     // std::function<void( String, String, String )> fns = [=]( String topic, String msg,
    //     //     String originator ) {
    //     //         this->subsNetSet( topic, msg, originator );
    //     // };
    //     // pSched->subscribe( tID, "net/network/set", fns );
    //     // std::function<void( String, String, String )> fnsg = [=]( String topic, String msg,
    //     //     String originator ) {
    //     //         this->subsNetsGet( topic, msg, originator );
    //     // };
    //     // pSched->subscribe( tID, "net/networks/get", fnsg );
    //     // std::function<void( String, String, String )> fsg = [=]( String topic, String msg,
    //     //     String originator ) {
    //     //         this->subsNetServicesGet( topic, msg, originator );
    //     // };
    //     // pSched->subscribe( tID, "net/services/+/get", fsg );
    // }

  private:
    void loop() {
        switch (curState) {
        case NOTDEFINED:
            break;
        case NOTCONFIGURED:
            if (connectionMonitor.beat()) {
                publishState();
            }
            break;
        case CONNECTINGAP:
            if (WiFi.status() == WL_CONNECTED) {
                curState = CONNECTED;
                DBG("Connected with ip address " + WiFi.localIP().toString());
                configureTime();
                return;
            }
            if (timeDiff(conTime, millis()) > connectTimeout) {
                DBG("Timout connecting to AP " + config.readString("net/station/SSID"));
                if (bOnceConnected) {
                    if (bRebootOnContinuedWifiFailure) {
                        --deathCounter;
                    }
                    if (deathCounter == 0) {
                        DBG("Final connection failure, restarting...");
                        if (bRebootOnContinuedWifiFailure) {
                            ESP.restart();
                        }
                    }
                    DBG("Reconnecting...");
                    WiFi.reconnect();
                    conTime = millis();
                } else {
                    DBG("Retrying to connect...");
                    if (initialCounter > 0) {
                        if (bRebootOnContinuedWifiFailure) {
                            --initialCounter;
                        }
                        WiFi.reconnect();
                        conTime = millis();
                        curState = CONNECTINGAP;
                    } else {
                        DBG("Final connect failure, configuration invalid?");
                        curState = NOTCONFIGURED;
                        if (bRebootOnContinuedWifiFailure) {
                            ESP.restart();
                        }
                    }
                }
            }
            break;
        case CONNECTED:
            bOnceConnected = true;
            deathCounter = reconnectMaxRetries;
            if (connectionMonitor.beat()) {
                if (WiFi.status() == WL_CONNECTED) {
                    long rssi = WiFi.RSSI();
                    if (rssival.filter(&rssi)) {
                        pSched->publish("net/rssi", String(rssi));
                    }
                } else {
                    WiFi.reconnect();
                    curState = CONNECTINGAP;
                    conTime = millis();
                }
            }
            break;
        case SERVING:
            unsigned int conns = WiFi.softAPgetStationNum();
            if (conns != connections) {
                connections = conns;
                pSched->publish("net/connections", String(connections));
            }
            if (statePublisher.beat()) {
                publishState();
            }
            break;
        }
        // react to state transition
        if (curState != oldState) {
            DBGP("Net State ");
            DBGP(getStringFromState(oldState));
            DBGP(" -> ");
            DBGP(getStringFromState(curState));
            if (curState == CONNECTED) {
                DBGP(", RSSI: ");
                DBG(WiFi.RSSI());
            } else {
                DBG();
            }
            switch (curState) {
            case SERVING:
            case CONNECTED:
                if (signalLed != 0xff) {
                    setLed(true);  // Turn the LED on
                }
                break;
            default:
                if (signalLed != 0xff) {
                    setLed(false);  // Turn the LED off
                }
                break;
            }
            oldState = curState;
            publishState();
        }
        // handle scanning
        if (scanning) {
            processScan(WiFi.scanComplete());
        }
    }

    void readNetConfig() {
        long version = config.readLong("net/version", 0);
        if (version == 0) {
            if (config.exists("net/SSID")) {
                // pre version configuration file found
                migrateNetConfigFrom(config, version);
                config.clear();
            }
        } else if (version < NET_CONFIG_VERSION) {
            // regular migration
            migrateNetConfigFrom(config, version);
            config.clear();
        }

        // mode and device id
        mode = getModeFromString(config.readString("net/mode"), AP);
        deviceID = config.readString("net/deviceid");
        if (deviceID == "") {
            // initialize device id to mac address
            deviceID = hexAddress;
            config.writeString("net/deviceid", deviceID);
        }

        // read some cached values
        reconnectMaxRetries = config.readLong("net/station/maxRetries", 1, 1000000000, 40);
        connectTimeout = config.readLong("net/station/connectTimeout", 3, 3600, 15) * 1000;
        bRebootOnContinuedWifiFailure = config.readBool("net/station/rebootOnFailure", true);
    }

    void migrateNetConfigFrom(JsonFile &sf, long version) {
        if (version == 0) {
            // full migration
            JsonFile nf(false, true);  // no autocommit, force new
            nf.writeLong("net/version", NET_CONFIG_VERSION);
            nf.writeString("net/mode", "station");
            nf.writeString("net/station/SSID", sf.readString("net/SSID"));
            nf.writeString("net/station/password", sf.readString("net/password"));
            nf.writeString("net/station/hostname", sf.readString("net/hostname"));
            ustd::array<JSONVar> services;
            if (sf.readJsonVarArray("net/services", services)) {
                for (unsigned int i = 0; i < services.length(); i++) {
                    DBG("Processing service " + String(i));
                    if (JSON.typeof(services[i]) == "object") {
                        if (JSON.typeof(services[i]["timeserver"]) == "string") {
                            String ntphost = (const char *)(services[i]["timeserver"]);
                            DBG("Found timeserver host entry: " + ntphost);
                            JSONVar host;
                            host[0] = (const char *)ntphost.c_str();
                            nf.writeJsonVar("net/services/ntp/host", host);
                        } else if (JSON.typeof(services[i]["dstrules"]) == "string") {
                            String dstrules = (const char *)(services[i]["dstrules"]);
                            DBG("Found timeserver dstrules entry: " + dstrules);
                            nf.writeString("net/services/ntp/dstrules", dstrules);
                        } else if (JSON.typeof(services[i]["mqttserver"]) == "string") {
                            String mqttserver = (const char *)(services[i]["mqttserver"]);
                            DBG("Found mqtt host entry: " + mqttserver);
                            JsonFile mqtt;
                            mqtt.writeString("mqtt/host", mqttserver);
                        }
                    } else {
                        DBG("Wrong service entry");
                    }
                }
            }
            nf.commit();
        }
        // else if ( version == 1 ) {

        // }
        // else if ( version == 2 ) {

        // }
    }

    void startServices() {
        wifiSetMode(mode);
        switch (mode) {
        case Netmode::OFF:
            DBG("Network is disabled");
            break;
        case Netmode::AP:
            if (startAP()) {
                curState = Netstate::SERVING;
            }
            break;
        case Netmode::STATION:
            if (startSTATION()) {
                curState = Netstate::CONNECTINGAP;
            }
            break;
        case Netmode::BOTH:
            if (startSTATION()) {
                curState = Netstate::CONNECTINGAP;
                startAP(false);
            }
            break;
        }
    }

    bool startAP(bool setHostname = true) {
        // configure hostname
        String hostname = replaceVars(config.readString("net/ap/hostname", "muwerk-${macls}"));
        if (!hostname.length()) {
            hostname = replaceVars("muwerk-${macls}");
        }
        if (setHostname) {
            DBG("Hostname := " + hostname);
            wifiSetHostname(hostname);
        }

        // configure network
        String address = config.readString("net/ap/address");
        String netmask = config.readString("net/ap/netmask");
        String gateway = config.readString("net/ap/gateway");
        if (address.length() & netmask.length()) {
            wifiSoftAPConfig(address, gateway, netmask);
        }

        // configure AP
        String SSID = replaceVars(config.readString("net/ap/SSID", "muwerk-${macls}"));
        if (!SSID.length()) {
            SSID = replaceVars("muwerk-${macls}");
        }
        String password = config.readString("net/ap/password");
        unsigned int channel = config.readLong("net/ap/channel", 1, 13, 1);
        bool hidden = config.readBool("net/ap/hidden", false);
        unsigned int maxConnections = config.readLong("net/ap/maxConnections", 1, 8, 4);
        connections = 0;

        DBG("Starting AP for SSID " + SSID + "...");
        if (wifiSoftAP(SSID, password, channel, hidden, maxConnections)) {
            if (setHostname) {
                wifiSetHostname(hostname);
            }
            DBG("AP Serving");
            return true;
        } else {
            DBG("AP Failed");
            return false;
        }
    }

    bool startSTATION() {
        // configure hostname
        String hostname = replaceVars(config.readString("net/station/hostname"));
        if (hostname) {
            wifiSetHostname(hostname);
        }

        // get connection parameters
        String SSID = config.readString("net/station/SSID");
        String password = config.readString("net/station/password");

        // get network parameter
        ustd::array<String> dns;
        String address = config.readString("net/station/address");
        String netmask = config.readString("net/station/netmask");
        String gateway = config.readString("net/station/gateway");
        config.readStringArray("net/services/dns/host", dns);

        DBG("Connecting AP with SSID " + config.readString("net/station/SSID"));

        if (wifiBegin(SSID, password)) {
            deathCounter = reconnectMaxRetries;
            initialCounter = reconnectMaxRetries;
            bOnceConnected = false;
            curState = CONNECTINGAP;
            conTime = millis();
            if (!wifiConfig(address, gateway, netmask, dns)) {
                DBG("Failed to set station mode configuration");
            }
            if (hostname) {
                wifiSetHostname(hostname);
            }
            configureTime();
            return true;
        }
        return false;
    }

    void configureStation() {
    }

    void configureTime() {
        ustd::array<String> ntpHosts;
        String ntpDstRules = config.readString("net/services/ntp/dstrules");
        config.readStringArray("net/services/ntp/host", ntpHosts);

        if (ntpDstRules.length() && ntpHosts.length()) {
            // configure ntp servers AND TZ variable
            configTzTime(ntpDstRules.c_str(), ntpHosts[0].c_str(),
                         ntpHosts.length() > 1 ? ntpHosts[1].c_str() : nullptr,
                         ntpHosts.length() > 2 ? ntpHosts[2].c_str() : nullptr);
        } else if (ntpHosts.length()) {
            // configure ntp servers without TZ variable
            configTime(0, 0, ntpHosts[0].c_str(),
                       ntpHosts.length() > 1 ? ntpHosts[1].c_str() : nullptr,
                       ntpHosts.length() > 2 ? ntpHosts[2].c_str() : nullptr);
        } else if (ntpDstRules.length()) {
            // configure only TZ variable
            setenv("TZ", ntpDstRules.c_str(), 3);
        } else {
            // take from RTC?
        }
    }

    void publishState() {
        JSONVar net;

        net["mode"] = getStringFromMode(mode);
        net["mac"] = macAddress;

        switch (curState) {
        case NOTCONFIGURED:
            net["state"] = "notconfigured";
            break;
        case CONNECTINGAP:
            net["state"] = "connectingap";
            net["SSID"] = config.readString("net/station/SSID");
            break;
        case CONNECTED:
            net["state"] = "connected";
            net["SSID"] = config.readString("net/station/SSID");
            net["hostname"] = wifiGetHostname();
            net["ip"] = WiFi.localIP().toString();
            break;
        case SERVING:
            net["state"] = "serving";
            net["hostname"] = wifiGetHostname();
            break;
        default:
            net["state"] = "undefined";
            break;
        }
        if (mode == Netmode::AP || mode == Netmode::BOTH) {
            net["ap"]["SSID"] = replaceVars(config.readString("net/ap/SSID", "muwerk-${macls}"));
            net["ap"]["ip"] = WiFi.softAPIP().toString();
            net["ap"]["mac"] = WiFi.softAPmacAddress();
            net["ap"]["connections"] = (int)connections;
        }
        String json = JSON.stringify(net);
        pSched->publish("net/network", json);
    }

    static String shift(String &args, char separator = ' ', String defValue = "") {
        /*! Extract the first arg from the supplied args
         * @param args The string object from which to shift out an argument
         * @param separator The separator character used for the shift operation
         * @param defValue (optional, default empty string) Default value to return if no more
         * args
         * @return The extracted arg
         */
        if (args == "") {
            return defValue;
        }
        int ind = args.indexOf(separator);
        String ret = defValue;
        if (ind == -1) {
            ret = args;
            args = "";
        } else {
            ret = args.substring(0, ind);
            args = args.substring(ind + 1);
            args.trim();
        }
        return ret;
    }

    void requestScan(String scantype = "async") {
        bool async = true;
        bool hidden = false;
        for (String arg = shift(scantype, ','); arg.length(); arg = shift(scantype, ',')) {
            arg.toLowerCase();
            if (arg == "sync") {
                async = false;
            } else if (arg == "async") {
                async = true;
            } else if (arg == "hidden") {
                hidden = true;
            }
        }

        processScan(WiFi.scanNetworks(async, hidden));
    }

    void processScan(int result) {
        switch (result) {
        case WIFI_SCAN_RUNNING:
            if (!scanning) {
                DBG("WiFi scan running...");
                scanning = true;
            }
            break;
        case WIFI_SCAN_FAILED:
            DBG("WiFi scan FAILED.");
            publishScan(result);
            scanning = false;
        case 0:
            DBG("WiFi scan succeeded: No network found.");
            scanning = false;
            publishScan(result);
            break;
        default:
            DBGF("WiFi scan succeeded: %u networks found.\r\n", result);
            scanning = false;
            publishScan(result);
        }
    }

    void publishScan(int result) {
        JSONVar res;

        res["result"] = result < 0 ? "error" : "ok";
        res["networks"] = JSON.parse("[]");

        for (int i = 0; i < result; i++) {
            JSONVar network;

            network["ssid"] = WiFi.SSID(i);
            network["rssi"] = WiFi.RSSI(i);
            network["channel"] = WiFi.channel(i);
            network["encryption"] = getStringFromEncryption(WiFi.encryptionType(i));
            network["bssid"] = WiFi.BSSIDstr(i);
#ifndef __ESP32__
            network["hidden"] = WiFi.isHidden(i);
#endif
            res["networks"][i] = network;
        }
        pSched->publish("net/networks", JSON.stringify(res));
    }

    void setLed(bool on) {
        if (signalLed != 0xff) {
            if (signalLogic) {
                digitalWrite(signalLed, on ? HIGH : LOW);
            } else {
                digitalWrite(signalLed, on ? LOW : HIGH);
            }
        }
    }

    String replaceVars(String val) {
        val.replace("${mac}", hexAddress);
        val.replace("${macls}", hexAddress.substring(6));
        val.replace("${macfs}", hexAddress.substring(0, 5));
        return val;
    }

    void updateMacAddr() {
        WiFiMode_t original = WiFi.getMode();
        WiFi.mode(WIFI_AP_STA);
        apmAddress = WiFi.softAPmacAddress();
        macAddress = WiFi.macAddress();
        hexAddress = macAddress;
        hexAddress.replace(":", "");
        WiFi.mode(original);
    }

    Netmode getModeFromString(String val, Netmode defVal = AP) {
        val.toLowerCase();
        if (val == "ap") {
            return AP;
        } else if (val == "station") {
            return STATION;
        } else if (val == "both") {
            return BOTH;
        } else {
            return defVal;
        }
    }

    const char *getStringFromEncryption(int encType) {
        // read the encryption type and print out the name:
#if !defined(__ESP32__)
        switch (encType) {
        case ENC_TYPE_WEP:
            return "WEP";
        case ENC_TYPE_TKIP:
            return "WPA";
        case ENC_TYPE_CCMP:
            return "WPA2";
        case ENC_TYPE_NONE:
            return "None";
        case ENC_TYPE_AUTO:
            return "Auto";
        default:
            return "unknown";
        }
#else
        switch (encType) {
        case WIFI_AUTH_OPEN:
            return "None";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2_ENTERPRISE";
        default:
            return "unknown";
        }
#endif
    }

    String getStringFromMode(Netmode val) {
        switch (val) {
        case AP:
            return "ap";
        case STATION:
            return "station";
        case BOTH:
            return "both";
        default:
            return "ap";
        }
    }

    String getStringFromState(Netstate val) {
        switch (val) {
        case NOTDEFINED:
            return "NOTDEFINED";
        case NOTCONFIGURED:
            return "NOTCONFIGURED";
        case SERVING:
            return "SERVING";
        case CONNECTINGAP:
            return "CONNECTINGAP";
        case CONNECTED:
            return "CONNECTED";
        default:
            return "UNKNOWN";
        }
    }
    static String wifiGetHostname() {
#if defined(__ESP32__)
        return WiFi.getHostname();
#else
        return WiFi.hostname();
#endif
    }

    static void wifiSetHostname(String hostname) {
#if defined(__ESP32__)
        WiFi.setHostname(hostname.c_str());
#else
        WiFi.hostname(hostname.c_str());
#endif
    }

    static bool wifiSoftAP(String ssid, String passphrase, unsigned int channel, bool hidden,
                           unsigned int max_connection) {
#if defined(__ESP32__)
        return WiFi.softAP(ssid.c_str(), passphrase.c_str(), channel, hidden, max_connection);
#else
        return WiFi.softAP(ssid, passphrase, channel, hidden, max_connection);
#endif
    }

    static bool wifiSoftAPConfig(String address, String gateway, String netmask) {
        IPAddress addr;
        IPAddress gate;
        IPAddress mask;
        addr.fromString(address);
        mask.fromString(netmask);
        if (gateway.length()) {
            gate.fromString(gateway);
        }
        return WiFi.softAPConfig(addr, gate, mask);
    }

    static bool wifiConfig(String address, String gateway, String netmask,
                           ustd::array<String> &dns) {
        IPAddress addr;
        IPAddress gate;
        IPAddress mask;
        IPAddress dns1;
        IPAddress dns2;
        if (address.length() && netmask.length()) {
            DBG("Setting static ip: " + address + " " + netmask);
            addr.fromString(address);
            mask.fromString(netmask);
        }
        if (gateway.length()) {
            DBG("Setting static gateway: " + gateway);
            gate.fromString(gateway);
        }
        if (dns.length() > 0) {
            DBG("Setting dns server 1: " + String(dns[0]));
            dns1.fromString(dns[0]);
        }
        if (dns.length() > 1) {
            DBG("Setting dns server 2: " + String(dns[1]));
            dns2.fromString(dns[1]);
        }
        return WiFi.config(addr, gate, mask, dns1, dns2);
    }

    static bool wifiBegin(String ssid, String passphrase) {
#ifdef __ESP32__
        return WiFi.begin(ssid.c_str(), passphrase.c_str());
#else
        return WiFi.begin(ssid, passphrase);
#endif
    }

    static void wifiSetMode(Netmode nm) {
        switch (nm) {
        case OFF:
            WiFi.mode(WIFI_OFF);
            break;
        default:
        case AP:
            WiFi.mode(WIFI_AP);
            break;
        case STATION:
            WiFi.mode(WIFI_STA);
            break;
        case BOTH:
            WiFi.mode(WIFI_AP_STA);
            break;
        }
    }
};  // class Net
}  // namespace ustd

// #endif  // defined(__ESP__)
