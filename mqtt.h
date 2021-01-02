// MQTT.h

#pragma once

// #if defined(__ESP__)

#include <functional>

#include <Arduino_JSON.h>
#include <PubSubClient.h>  // ESP32 requires v2.7 or later

#include "platform.h"
#include "array.h"
#include "map.h"

#include "scheduler.h"

namespace ustd {
class Mqtt {
  private:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    bool bMqInit = false;
    Scheduler *pSched;
    String domainToken = "mu";
    String outDomainToken = "omu";
    String outDomainPrefix;  // outDomainToken + '/' + clientName, or just clientName, if
                             // outDomainToken==""
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
    String mqttUsername = "";
    String mqttPassword = "";
    String willTopic = "";
    String willMessage = "";
    ustd::array<String> subsList;

  public:
    Mqtt() {
        /*! Instantiate a MQTT client object using pubsubclient library.
         *
         * This object connects to an external MQTT server as soon as a network
         * connection is available.
         *
         * The muwerk scheduler implements pub/sub inter-task communication
         * between muwerk tasks. Tasks can subscribe to MQTT formatted topics
         * and publish messages.
         *
         * Publish to external server:
         *
         * This object extends the internal communication
         * to external MQTT servers. All internal muwerk messages are published
         * to the external MQTT server with prefix <outDomainPrefix>/<hostname>/.
         * E.g. if a muwerk task ustd::Scheduler.publish('led/set', 'on'); and
         * hostname of ESP is 'myhost', an MQTT publish message with topic
         * 'omu/myhost/led/set' and msg 'on' is sent to the external server.
         * Default outDomainPrefix is 'omu'.
         * In order to publish to an unmodified topic, prefix the topic with '!',
         * then neither outDomainPrefix nor hostname are prepended. E.g. publish
         * to topic !system/urgent will cause an MQTT publish to system/urgent
         * with no additional prefixes. Note: this can cause recursions.
         *
         * Subscribes to external server:
         *
         * This object subscribes to two wild-card topics on the external server:
         *
         * <hostname>/#
         *
         * and
         *
         * <domainToken>/#
         *
         * Hostname is by default the hostname of the ESP chip, but can
         * be overwritten using
         * _clientName in ustd::Mqtt.begin();. domainToken is "mu" by
         * default and can be overwritten using _domainToken in
         * ustd::Mqtt.begin();
         *
         * Received messsages are stripped of hostname or domainToken prefix
         * and published into ustd::Scheduler.publish();. That way external
         * MQTT messages are routed to any muwerk task that uses the internal
         * muwerk ustd::Scheduler.subscribe(); mechanism, and all muwerk
         * tasks can publish to external MQTT entities transparently.
         *
         * Additionally, arbitrary topics can be subscribed to via addSubscription().
         * Topics that are added via addSubscription() are transparently forwarded.
         * Nothing is stripped, and it is user's responsibility to prevent loops.
        \code{cpp}
        #define __ESP__ 1   // Platform defines required, see doc, mainpage.
        #include "scheduler.h"
        #include "net.h"
        #include "mqtt.h"

        ustd::Scheduler sched;
        ustd::Net net();
        ustd::Mqtt mqtt;

        void setup() {
            net.begin(&sched);
            mqtt.begin(&sched);
        }
        \endcode
         */
        mqttServer = "";
        isOn = false;
    }

    ~Mqtt() {
        if (isOn) {
            isOn = false;
        }
    }

    void begin(Scheduler *_pSched, String _clientName = "", String _domainToken = "",
               String _outDomainToken = "omu", String _mqttUsername = "", String _mqttPassword = "",
               String _willTopic = "", String _willMessage = "") {
        /*! Connect to external MQTT server as soon as network is available
         *
         * This function starts the connection to an MQTT server.
         *
         * @param _pSched Pointer to the muwerk scheduler.
         * @param _clientName (optional, default is hostname) the MQTT client
         * name. WARNING: this name mus tbe unique! Otherwise the MQTT server
         * will rapidly disconnect.
         * @param _domainToken (optional, default is "mu") The MQTT client subs
         * to message topics <_clientName>/# and <_domainToken>/#, strips both
         * _domainToken and _clientName from received topics and publishes those
         * messages to the internal muwerk interface ustd::scheduler.publish();
         * @param _outDomainToken (optional, default is "omu") All publications
         * from this client to outside MQTT-servers have their topic prefixed
         * by <outDomainName>/<clientName>/topic. This is to prevent recursions.
         * @param _mqttUsername Username for mqtt server authentication, leave
         * empty "" for no username.
         * @param _mqttPassword Password for mqtt server authentication, leave
         * empty "" for no password.
         * @param _willTopic Topic of mqtt last will. Default is "omu/<clientName>/mqtt/state".
         * Note: it is not recommended to change will-configuration, when using the home-assistant
         * configuration.
         * @param _willMessage Message content for last will message. Default is "disconnected"
         */

        pSched = _pSched;
        clientName = _clientName;
        mqttClient = wifiClient;

        mqttTicker = millis();
        if (_domainToken != "") {
            domainToken = _domainToken;
        }
        if (_outDomainToken != "omu") {
            outDomainToken = _outDomainToken;
        }
        if (clientName == "") {
#if defined(__ESP32__)
            clientName = WiFi.getHostname();
#else
            clientName = String(WiFi.hostname().c_str());
#endif
        }

        if (outDomainToken == "")
            outDomainPrefix = clientName;
        else
            outDomainPrefix = outDomainToken + "/" + clientName;
        if (_willTopic == "") {
            willTopic = outDomainPrefix + "/mqtt/state";
            willMessage = "disconnected";
        } else {
            willTopic = _willTopic;
            willMessage = _willMessage;
        }
        mqttUsername = _mqttUsername;
        mqttPassword = _mqttPassword;

        // give a c++11 lambda as callback scheduler task registration of
        // this.loop():
        std::function<void()> ft = [=]() { this->loop(); };
        tID = pSched->add(ft, "mqtt");

        std::function<void(String, String, String)> fnall = [=](String topic, String msg,
                                                                String originator) {
            this->subsMsg(topic, msg, originator);
        };
        pSched->subscribe(tID, "#", fnall);

        pSched->publish("net/network/get");
        pSched->publish("net/services/mqttserver/get");
        isOn = true;
    }

    int addSubscription(int taskID, String topic, T_SUBS subs, String originator = "") {
        /*! Subscribe via MQTT server to a topic to receive messages published to this topic
         *
         * This function is similar to muwerk's subscribe() function, but in
         * addition, this function does an external MQTT subscribe. By default,
         * munet's mqtt only subscribes to topics that either start with
         * clientName or with an optional domainName. Via this function, arbitrary
         * MQTT subscriptions can be added.
         *
         * addSubscription() subscribes on two layers: locally to muwerk's scheduler,
         * and externally with the MQTT server.
         *
         * @param taskID taskID of the task that is associated with this
         * subscriptions (only used for statistics)
         * @param topic MQTT-style topic to be subscribed, can contain MQTT
         * wildcards '#' and '*'. (A subscription to '#' receives all pubs)
         * @param subs Callback of type void myCallback(String topic, String
         * msg, String originator) that is called, if a matching message is
         * received. On ESP or Unixoid platforms, this can be a member function.
         * @param originator Optional name of associated task.
         * @return subscriptionHandle on success (needed for unsubscribe), or -1
         * on error.
         */

        int handle;
        pSched->subscribe(taskID, topic, subs, originator);
        for (unsigned int i = 0; i < subsList.length(); i++) {
            if (topic == subsList[i])
                return handle;  // Already subbed via mqtt.
        }
        if (mqttConnected) {
            mqttClient.subscribe(topic.c_str());
        }
        subsList.add(topic);
        return handle;
    }

    bool removeSubscription(int subscriptionHandle, String topic) {
        /*! Unsubscribe a subscription
         *
         * @param subscriptionHandle Handle to subscription as returned by
         * Subscribe(), used for unsubscribe with muwerk's scheduler.
         * @param topic The topic string that was used in addSubscription, used for
         * unsubscribe via MQTT server.
         * @return true on successful unsubscription, false if no corresponding
         * subscription is found.
         */

        bool ret = pSched->unsubscribe(subscriptionHandle);
        for (unsigned int i = 0; i < subsList.length(); i++) {
            if (topic == subsList[i])
                subsList.erase(i);
        }
        return ret;
    }

  private:
    bool bWarned = false;
    void loop() {
        if (isOn) {
            if (netUp && mqttServer != "") {
                if (mqttConnected) {
                    mqttClient.loop();
                }

                if (bCheckConnection || timeDiff(mqttTicker, millis()) > mqttTickerTimeout) {
                    mqttTicker = millis();
                    bCheckConnection = false;
                    if (!mqttClient.connected()) {
                        // Attempt to connect
                        const char *usr = NULL;
                        const char *pwd = NULL;
                        if (mqttUsername != "")
                            usr = mqttUsername.c_str();
                        if (mqttPassword != "")
                            pwd = mqttPassword.c_str();
                        bool conRes = false;
                        // if (willTopic == "") {  // xxx can't happen any more
                        //    conRes = mqttClient.connect(clientName.c_str(), usr, pwd);
                        //} else {
                        conRes = mqttClient.connect(clientName.c_str(), usr, pwd, willTopic.c_str(),
                                                    0, true, willMessage.c_str());
                        //}
                        if (conRes) {
#ifdef USE_SERIAL_DBG
                            Serial.println("Connected to mqtt server");
#endif
                            mqttConnected = true;
                            mqttClient.subscribe((clientName + "/#").c_str());
                            mqttClient.subscribe((domainToken + "/#").c_str());
                            for (unsigned int i = 0; i < subsList.length(); i++) {
                                mqttClient.subscribe(subsList[i].c_str());
                            }
                            bWarned = false;
                            // if (willTopic == "") {
                            //    pSched->publish("mqtt/state",
                            //                    "connected," + outDomainPrefix);
                            //} else {
                            self.config_message =
                                outDomainPrefix + "+" + willTopic + "+" + willMessage;
                            pSched->publish("mqtt/config", self.config_message);
                            pSched->publish("mqtt/state", "connected");
                            // pSched->publish("mqtt/config", outDomainPrefix +
                            // "+" + willTopic + "+" + willMessage);
                            //}
                        } else {
                            mqttConnected = false;
                            if (!bWarned) {
                                bWarned = true;
                                pSched->publish("mqtt/state", "disconnected");
                                //+ outDomainPrefix);
#ifdef USE_SERIAL_DBG
                                Serial.println("MQTT disconnected.");
#endif
                            }
                        }
                    }
                }
            }
        }
    }

    void mqttReceive(char *ctopic, unsigned char *payload, unsigned int length) {
        String msg;
        String topic;
        String tokn;
        ustd::array<String> toks;

        msg = "";
        for (unsigned int i = 0; i < length; i++) {
            msg += (char)payload[i];
        }
        topic = String(ctopic);
        for (unsigned int i = 0; i < subsList.length(); i++) {
            if (pSched->mqttmatch(topic, subsList[i])) {
                pSched->publish(topic, msg, "mqtt");
                return;
            }
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
            String tpc;
            if (topic.c_str()[0] == '!') {
                tpc = &(topic.c_str()[1]);
            } else {
                tpc = outDomainPrefix + "/" + topic;
            }
            bool bRetain = true;
#ifdef USE_SERIAL_DBG
            Serial.println("MQTT: publishing...");
#endif
            if (mqttClient.publish(tpc.c_str(), msg.c_str(), bRetain)) {
#ifdef USE_SERIAL_DBG
                Serial.println(("MQTT publish: " + topic + " | " + String(msg)).c_str());
#endif
            } else {
#ifdef USE_SERIAL_DBG
                Serial.println(("MQTT ERROR len=" + String(len) + ", not published: " + topic +
                                " | " + String(msg))
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
        /*
        DynamicJsonBuffer jsonBuffer(512);
        JsonObject &root = jsonBuffer.parseObject(msg);

        if (!root.success()) {
#ifdef USE_SERIAL_DBG
            Serial.println(
                ("mqtt: Invalid JSON received: " + String(msg)).c_str());
#endif
            return;
        }
        */
        JSONVar mqttJsonMsg = JSON.parse(msg);

        if (JSON.typeof(mqttJsonMsg) == "undefined") {
            return;
        }

        if (topic == "mqtt/state/get") {
            if (mqttConnected) {
                pSched->publish("mqtt/state", "connected");
            } else {
                pSched->publish("mqtt/state", "disconnected");
            }
        }
        if (topic == "mqtt/config/get") {
            if (mqttConnected) {
                pSched->publish("mqtt/config", self.config_message);
            } else {
                pSched->publish("mqtt/config", self.config_message);
            }
        }
        if (topic == "net/services/mqttserver") {
            if (!bMqInit) {
                mqttServer = (const char *)mqttJsonMsg["server"];  // root["server"].as<char *>();
                bCheckConnection = true;
                mqttClient.setServer(mqttServer.c_str(), 1883);
                // give a c++11 lambda as callback for incoming mqtt
                // messages:
                std::function<void(char *, unsigned char *, unsigned int)> f =
                    [=](char *t, unsigned char *m, unsigned int l) { this->mqttReceive(t, m, l); };
                // If this breaks for ESP32, update pubsubclient to v2.7 or
                // newer
                mqttClient.setCallback(f);
                bMqInit = true;
#ifdef USE_SERIAL_DBG
                Serial.println("MQTT received config info.");
#endif
            }
        }
        if (topic == "net/network") {
            String state = (const char *)mqttJsonMsg["state"];  // root["state"];
            if (state == "connected") {
#ifdef USE_SERIAL_DBG
                Serial.println("MQTT received network connect");
#endif
                if (!netUp) {
                    netUp = true;
                    bCheckConnection = true;
#ifdef USE_SERIAL_DBG
                    Serial.println("MQTT net state online");
#endif
                }
            } else {
                netUp = false;
#ifdef USE_SERIAL_DBG
                Serial.println("MQTT net state offline");
#endif
            }
        }
    };
};

}  // namespace ustd

// #endif  // defined(__ESP__)
