// net.h - the muwerk network

#pragma once

// #if defined(__ESP__)

#include "platform.h"
#include "array.h"
#include "map.h"

#include "scheduler.h"
#include "sensors.h"
#include "muwerk.h"
#include "jsonfile.h"
#include "heartbeat.h"
#include "timeout.h"

namespace ustd {

/*! \brief munet, the muwerk network class for WiFi and NTP

The library header-only and relies on the libraries ustd, muwerk, Arduino_JSON and PubSubClient.

Make sure to provide the <a
href="https://github.com/muwerk/ustd/blob/master/README.md">required platform define</a> before
including ustd headers.

See <a
href="https://github.com/muwerk/munet/blob/master/README.md">README.md</a> for a detailed
description of all network configuration options.

Alternatively, operating mode and credentials can be given in source code during Net.begin(). (s.b.)

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
    Netmode defaultMode;
    bool defaultReboot;
    ustd::jsonfile config;

    // hardware info
    String apmAddress;
    String macAddress;
    String deviceID;

    // runtime control - state management
    Netmode mode;
    Netstate curState;
    Netstate oldState;
    ustd::heartbeat statePublisher = 30000;
    // runtime control - station connection management
    ustd::heartbeat connectionMonitor = 1000;
    ustd::timeout connectTimeout = 15000;
    unsigned int reconnectMaxRetries = 40;
    bool bRebootOnContinuedFailure = true;
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
        /*! Instantiate a network object for WiFi and NTP connectivity.
         *
         * The Net object publishes messages using muwerk's pub/sub intertask communication (which
         * does not rely on MQTT servers), other muwerk tasks can subscribe to the following topics:
         *
         * * `net/network`: for information about WiFi connection state changes. Status can be
         * actively requested by publishing `net/network/get`
         * * `net/rssi`: for information about WiFi signal strength
         * * `net/connections`: for changes in the number of connected clients when operating as
         * access point
         * * `net/networks`: for a list of WiFi networks nearby. The list can be requested by
         * publishing `net/networks/get`
         *
         * @param signalLed (optional), Pin that will be set to LOW (led on)
         * during network connection attempts. Once connected, led is switched
         * off and can be used for other functions. Led on signals that the ESP
         * is trying to connect to a network.
         * @param signalLogic (optional, default `false`) If `true` the signal logic for
         * the led is inverted.
         */
        oldState = NOTDEFINED;
        curState = NOTCONFIGURED;
    }

    void begin(Scheduler *pScheduler, Netmode opmode = AP, bool restartOnMultipleFailures = true) {
        /*! Starts the network based on the stored configuration.
         *
         * This method starts the network using the information stored into the configuration
         * file /net.json. Depending on how the network mode is configured, it may be idle (since
         * disabled), running as an access point or as a station or both.
         *
         * Other muwerk task can subscribe to topic '`net/network`' to receive
         * information about network connection states.
         *
         * @param pScheduler Pointer to the muwerk scheduler.
         * @param opmode (optional, default AP) Default operation mode if none is configured
         * @param restartOnMultipleFailures (optional, default `true`) Default restart on continued
         * failure if none is configured.
         *
         * See <a href="https://github.com/muwerk/munet/blob/master/README.md">README.md</a> for a
         * detailed description of all network configuration options.
         */
        initLed();
        initHardwareAddresses();
        readNetConfig(opmode, restartOnMultipleFailures);
        initScheduler(pScheduler);
        startServices();
    }

    void begin(Scheduler *pScheduler, String SSID, String password = "",
               String hostname = "muwerk-${macls}", Netmode opmode = AP,
               bool restartOnMultipleFailures = true) {
        /*! Starts the network based on the supplied configuration.
         *
         * This function starts the network using the supplied information and is intended for
         * projects with hardcoded network configuration. This method allows only to configure
         * the network running as an access point or as a station.
         *
         * Other muwerk task can subscribe to topic '`net/network`' to receive
         * information about network connection states.
         *
         * @param pScheduler Pointer to the muwerk scheduler.
         * @param SSID The SSID for the WiFi connection or access point. This can contin
         * placeholder (see below).
         * @param password (optional, default unused) The password for the WiFi connection or access
         * point
         * @param hostname (optional, default `"muwerk-${macls}"`) The hostname of the system. This
         * can contain placeholder (see below).
         * @param opmode (optional, default AP) The operating mode of the network. Can be AP or
         * STATION
         * @param restartOnMultipleFailures (optional, default `true`) restarts the device on
         * continued failure.
         *
         * In STATION mode the network connects to an available WiFi network using the supplied
         * credentials. After connecting, the system requests a network configuration via DHCP.
         * After receiving the configuration, the IP address, netmask and gateway are set. If
         * the DHCP server sends information about a valid NTP server, the time is synchronized
         * using the information from that server.
         *
         * Some of the configuration options support the use of placeholders in order to allow
         * values that are specific to a certain device without the need to create separate
         * configuration files. Placeholders are written in the form of `${PLACEHOLDER}`.
         *
         * The following placeholders are currently available:
         * * `mac`: full mac address
         * * `macls`: last 4 digits of mac address
         * * `macfs`: first 4 digits of mac address
         */
        if (opmode != Netmode::AP && opmode != Netmode::STATION) {
            DBG("ERROR: Wrong operation mode specified on Net::begin");
            return;
        }
        initLed();
        initHardwareAddresses();
        initNetConfig(SSID, password, hostname, opmode, restartOnMultipleFailures);
        initScheduler(pScheduler);
        startServices();
    }

  private:
    void initScheduler(Scheduler *pScheduler) {
        pSched = pScheduler;
        tID = pSched->add([this]() { this->loop(); }, "net");

        pSched->subscribe(
            tID, "net/network/get",
            [this](String topic, String msg, String originator) { this->publishState(); });

        pSched->subscribe(
            tID, "net/network/control",
            [this](String topic, String msg, String originator) { this->control(msg); });

        pSched->subscribe(
            tID, "net/networks/get",
            [this](String topic, String msg, String originator) { this->requestScan(msg); });
    }

    void loop() {
        if (mode == Netmode::OFF) {
            return;
        }
        // radio specific state handling
        switch (curState) {
        case NOTDEFINED:
        case NOTCONFIGURED:
            // states with inactive radio
            break;
        case CONNECTINGAP:
        case CONNECTED:
        case SERVING:
            // states with active radio
            unsigned int conns = WiFi.softAPgetStationNum();
            if (conns != connections) {
                connections = conns;
                pSched->publish("net/connections", String(connections));
            }
            break;
        }
        // individual per state handling
        switch (curState) {
        case NOTDEFINED:
            break;
        case NOTCONFIGURED:
            if (statePublisher.beat()) {
                publishState();
            }
            break;
        case CONNECTINGAP:
            if (WiFi.status() == WL_CONNECTED) {
                curState = CONNECTED;
                DBG("Connected to WiFi with ip address " + WiFi.localIP().toString());
                configureTime();
                return;
            }
            if (connectTimeout.test()) {
                DBG("Timout connecting to WiFi " + WiFi.SSID());
                if (bOnceConnected) {
                    if (bRebootOnContinuedFailure) {
                        --deathCounter;
                    }
                    if (deathCounter == 0) {
                        DBG("Final connection failure, restarting...");
                        if (bRebootOnContinuedFailure) {
                            ESP.restart();
                        }
                    }
                    DBG("Reconnecting...");
                    WiFi.reconnect();
                    connectTimeout.reset();
                } else {
                    DBG("Retrying to connect...");
                    if (initialCounter > 0) {
                        if (bRebootOnContinuedFailure) {
                            --initialCounter;
                        }
                        WiFi.reconnect();
                        connectTimeout.reset();
                        curState = CONNECTINGAP;
                    } else {
                        DBG("Final connect failure, configuration invalid?");
                        curState = NOTCONFIGURED;
                        if (bRebootOnContinuedFailure) {
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
                    connectTimeout.reset();
                }
            }
            break;
        case SERVING:
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

            // Turn the LED on when device is connecting to a WiFi
            setLed(curState == CONNECTINGAP);

            oldState = curState;
            publishState();
        }
        // handle scanning
        if (scanning) {
            processScan(WiFi.scanComplete());
        }
    }  // namespace ustd

    void control(String msg) {
        msg.toLowerCase();
        if (msg == "on" || msg == "start") {
            if (curState == Netstate::NOTDEFINED || curState == Netstate::NOTCONFIGURED) {
                startServices();
            }
        } else if (msg == "off" || msg == "stop") {
            stopServices();
        } else if (msg == "restart") {
            stopServices();
            pSched->publish("net/network/control", "start");
        }
    }

    void initNetConfig(String SSID, String password, String hostname, Netmode opmode,
                       bool restart) {
        // initialize default values
        defaultMode = opmode;
        defaultReboot = restart;

        String prefix = "net/" + getStringFromMode(opmode) + "/";

        // prepare mode and device id
        mode = opmode;
        deviceID = macAddress;
        deviceID.replace(":", "");

        // generate hardcoded configuration
        config.clear(false, true);
        config.writeBool("net/hardcoded", true);
        config.writeString("net/hostname", hostname);
        config.writeString("net/mode", getStringFromMode(opmode));
        config.writeString("net/deviceid", deviceID);
        config.writeString(prefix + "SSID", SSID);
        config.writeString(prefix + "password", password);
        config.writeBool("net/station/rebootOnFailure", restart);
    }

    void readNetConfig(Netmode opmode, bool restart) {
        // initialize default values
        defaultMode = opmode;
        defaultReboot = restart;

        // handle config version migrations
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

        // prepare mode and device id
        mode = getModeFromString(config.readString("net/mode"), defaultMode);
        deviceID = config.readString("net/deviceid");
        if (deviceID == "") {
            // initialize device id to mac address
            deviceID = macAddress;
            deviceID.replace(":", "");
            config.writeString("net/deviceid", deviceID);
        }
    }

    void cleanupNetConfig() {
        // free up memory - we reload the configuration on demand...
        if (!config.readBool("net/hardcoded", false)) {
            // but only if the configuration is NOT hardcoded...
            DBG("Freeing configuration...");
            config.clear();
        }
    }

    void migrateNetConfigFrom(ustd::jsonfile &sf, long version) {
        if (version == 0) {
            // convert the oldest version to the current version
            ustd::jsonfile nf(false, true);  // no autocommit, force new
            nf.writeLong("net/version", NET_CONFIG_VERSION);
            nf.writeString("net/mode", "station");
            nf.writeString("net/hostname", sf.readString("net/hostname"));
            nf.writeString("net/station/SSID", sf.readString("net/SSID"));
            nf.writeString("net/station/password", sf.readString("net/password"));
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
                            ustd::jsonfile mqtt(false, true);  // no autocommit, force new
                            mqtt.writeString("mqtt/host", mqttserver);
                            mqtt.writeBool("mqtt/alwaysRetained", true);
                            mqtt.commit();
                        }
                    } else {
                        DBG("Wrong service entry");
                    }
                }
            }
            nf.commit();
        }
        // implement here future conversions
        // else if ( version == 1 ) {
        // }
        // else if ( version == 2 ) {
        // }
    }

    void startServices() {
        mode = getModeFromString(config.readString("net/mode"), defaultMode);
        wifiSetMode(mode);
        switch (mode) {
        case Netmode::OFF:
            DBG("Network is disabled");
            curState = Netstate::NOTCONFIGURED;
            publishState();
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
                startAP();
            }
            break;
        }
        if (curState == Netstate::NOTCONFIGURED) {
            DBG("Failed to start network services");
            cleanupNetConfig();
        }
    }

    void stopServices() {
        switch (mode) {
        case Netmode::OFF:
            DBG("Network is disabled");
            publishState();
            break;
        case Netmode::AP:
            DBG("Stopping AP");
            WiFi.softAPdisconnect(false);
            break;
        case Netmode::STATION:
            DBG("Disconnecting from WiFi");
            WiFi.disconnect(false);
            break;
        case Netmode::BOTH:
            DBG("Disconnecting from WiFi and stopping AP");
            WiFi.disconnect(false);
            WiFi.softAPdisconnect(false);
            break;
        }
        scanning = false;
        connections = 0;
        curState = Netstate::NOTCONFIGURED;
        wifiSetMode(Netmode::OFF);
        cleanupNetConfig();
    }

    bool startAP() {
        // configure hostname
        String hostname = replaceVars(config.readString("net/hostname", "muwerk-${macls}"));
        wifiAPSetHostname(hostname);

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

        DBG("Starting AP with SSID " + SSID + "...");
        if (wifiSoftAP(SSID, password, channel, hidden, maxConnections)) {
            wifiAPSetHostname(hostname);
            DBG("AP Serving");
            return true;
        } else {
            DBG("AP Failed");
            return false;
        }
    }

    bool startSTATION() {
        // get connection parameters
        String hostname = replaceVars(config.readString("net/hostname"));
        String SSID = config.readString("net/station/SSID");
        String password = config.readString("net/station/password");

        // get network parameter
        ustd::array<String> dns;
        String address = config.readString("net/station/address");
        String netmask = config.readString("net/station/netmask");
        String gateway = config.readString("net/station/gateway");
        config.readStringArray("net/services/dns/host", dns);

        // read some cached values
        connectTimeout = config.readLong("net/station/connectTimeout", 3, 3600, 15) * 1000;
        reconnectMaxRetries = config.readLong("net/station/maxRetries", 1, 1000000000, 40);
        bRebootOnContinuedFailure = config.readBool("net/station/rebootOnFailure", defaultReboot);

        DBG("Connecting WiFi " + SSID);
        wifiSetHostname(hostname);
        if (wifiBegin(SSID, password)) {
            deathCounter = reconnectMaxRetries;
            initialCounter = reconnectMaxRetries;
            bOnceConnected = false;
            curState = CONNECTINGAP;
            connectTimeout.reset();
            if (!wifiConfig(address, gateway, netmask, dns)) {
                DBG("Failed to set network configuration");
            }
            wifiSetHostname(hostname);  // override dhcp option "host name"
            configureTime();
            return true;
        }
        return false;
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
            net["SSID"] = WiFi.SSID();
            break;
        case CONNECTED:
            net["state"] = "connected";
            net["SSID"] = WiFi.SSID();
            net["hostname"] = wifiGetHostname();
            net["ip"] = WiFi.localIP().toString();
            break;
        case SERVING:
            net["state"] = "serving";
            net["hostname"] = wifiAPGetHostname();
            break;
        default:
            net["state"] = "undefined";
            break;
        }
        if (curState != NOTCONFIGURED && (mode == Netmode::AP || mode == Netmode::BOTH)) {
            net["ap"]["mac"] = WiFi.softAPmacAddress();
            net["ap"]["SSID"] = replaceVars(config.readString("net/ap/SSID", "muwerk-${macls}"));
            net["ap"]["ip"] = WiFi.softAPIP().toString();
            net["ap"]["connections"] = (int)connections;
        }
        String json = JSON.stringify(net);
        pSched->publish("net/network", json);
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

    void initLed() {
        if (signalLed != 0xff) {
            pinMode(signalLed, OUTPUT);
            setLed(signalLogic);  // Turn the LED off
        }
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
        String hexAddress = macAddress;
        hexAddress.replace(":", "");
        val.replace("${mac}", hexAddress);
        val.replace("${macls}", hexAddress.substring(6));
        val.replace("${macfs}", hexAddress.substring(0, 5));
        return val;
    }

    void initHardwareAddresses() {
        WiFiMode_t currentMode = WiFi.getMode();
        WiFi.mode(WIFI_AP_STA);
        apmAddress = WiFi.softAPmacAddress();
        macAddress = WiFi.macAddress();
        WiFi.mode(currentMode);
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
        default:
        case Netmode::OFF:
            return "off";
        case Netmode::AP:
            return "ap";
        case Netmode::STATION:
            return "station";
        case Netmode::BOTH:
            return "both";
        }
    }

    const char *getStringFromState(Netstate val) {
        switch (val) {
        default:
        case Netstate::NOTDEFINED:
            return "NOTDEFINED";
        case Netstate::NOTCONFIGURED:
            return "NOTCONFIGURED";
        case Netstate::SERVING:
            return "SERVING";
        case Netstate::CONNECTINGAP:
            return "CONNECTINGAP";
        case Netstate::CONNECTED:
            return "CONNECTED";
        }
    }

    // ESP WiFi Abstraction

    static String wifiGetHostname() {
#if defined(__ESP32__)
        return WiFi.getHostname();
#else
        return WiFi.hostname();
#endif
    }

    void wifiSetHostname(String &hostname) {
        if (!hostname.length()) {
            hostname = replaceVars("muwerk-${macls}");
        }
#if defined(__ESP32__)
        WiFi.setHostname(hostname.c_str());
#else
        WiFi.hostname(hostname.c_str());
#endif
    }

#if !defined(__ESP32__)
    // since the ESP8266 is not able to manage a hostname for the soft station network,
    // we need to emulate it
    static String esp8266APhostname;
#endif

    static String wifiAPGetHostname() {
#if defined(__ESP32__)
        return WiFi.softAPgetHostname();
#else
        return Net::esp8266APhostname;
#endif
    }

    void wifiAPSetHostname(String &hostname) {
        if (!hostname.length()) {
            hostname = replaceVars("muwerk-${macls}");
        }
#if defined(__ESP32__)
        WiFi.softAPsetHostname(hostname.c_str());
#else
        Net::esp8266APhostname = hostname;
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

    static bool wifiSoftAPConfig(String &address, String &gateway, String &netmask) {
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

    static bool wifiConfig(String &address, String &gateway, String &netmask,
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

    static bool wifiBegin(String &ssid, String &passphrase) {
#if defined(__ESP32__)
        return WiFi.begin(ssid.c_str(), passphrase.c_str());
#else
        return WiFi.begin(ssid, passphrase);
#endif
    }

    static void wifiSetMode(Netmode val) {
        switch (val) {
        case Netmode::OFF:
            WiFi.mode(WIFI_OFF);
            break;
        default:
        case Netmode::AP:
            WiFi.mode(WIFI_AP);
            break;
        case Netmode::STATION:
            WiFi.mode(WIFI_STA);
            break;
        case Netmode::BOTH:
            WiFi.mode(WIFI_AP_STA);
            break;
        }
    }
};

#if !defined(__ESP32__)
// since the ESP8266 is not able to manage a hostname for the soft station network,
// we need to emulate it
String Net::esp8266APhostname = "";
#endif

}  // namespace ustd

// #endif  // defined(__ESP__)
