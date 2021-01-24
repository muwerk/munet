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
#include "timeout.h"

#include "munet.h"

namespace ustd {

/*! \brief munet MQTT Gateway Class

The muwerk scheduler implements pub/sub inter-task communication between muwerk tasks. Tasks can
subscribe to MQTT formatted topics and publish messages. The MQTT Class implements a gateway between
the muwerk scheduler's message queue and an external MQTT server.

### Publishing to external server:

This object extends the internal communication to external MQTT servers. All internal muwerk
messages are published to the external MQTT server with prefix `<outDomainPrefix>/<hostname>/`.
E.g. if a muwerk task `ustd::Scheduler.publish('led/set', 'on')` and `clientName` of ESP is
`myhost`, an MQTT publish message with topic `'omu/myhost/led/set'` and msg `on` is sent to the
external server. Default `outDomainPrefix` is `'omu'`. In order to publish to an unmodified topic,
prefix the topic with `'!'`, then neither `outDomainPrefix` nor `clientName` are prepended. E.g.
publish to topic `'!system/urgent'` will cause an MQTT publish to `'system/urgent'` with no
additional prefixes. Note: this can cause recursions.

### Subscribing from external server:

This object subscribes two wild-card topics on the external server:

  1. `<clientName>/#`
  2. `<domainToken>/#`

`<clientName>` is by default the hostname of the device chip, but can be overwritten using
`_clientName` in `ustd::Mqtt.begin();`. `domainToken` is "mu" by default and can be overwritten
using `_domainToken` in `ustd::Mqtt.begin()`;

Received messsages are stripped of `clientName` or `domainToken` prefix and published into
`ustd::Scheduler.publish()`. That way external MQTT messages are routed to any muwerk task that uses
the internal muwerk `ustd::Scheduler.subscribe()` mechanism, and all muwerk tasks can publish to
external MQTT entities transparently.

Additionally, arbitrary topics can be subscribed to via `addSubscription()`. Topics that are added
via `addSubscription()` are transparently forwarded. Nothing is stripped, and it is user's
responsibility to prevent loops.

## Sample MQTT Integration

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
class Mqtt {
  private:
    // muwerk task management
    Scheduler *pSched;
    int tID;

    // mqtt client
    WiFiClient wifiClient;
    PubSubClient mqttClient;

    // active configuration
    String mqttServer;
    uint16_t mqttServerPort;
    String mqttUsername;
    String mqttPassword;
    bool mqttRetained;
    String clientName;
    String domainToken;
    String outDomainToken;
    String lwTopic;
    String lwMsg;
    // computed configuration
    ustd::array<String> ownedPrefixes;
    bool bStateRetained;
    String outDomainPrefix;  // outDomainToken + '/' + clientName, or just clientName, if
                             // outDomainToken==""

    // persistently initialized tables
    ustd::array<String> subsList;
    ustd::array<String> outgoingBlockList;
    ustd::array<String> incomingBlockList;

    // runtime control - state management
    bool isOn = false;
    bool netUp = false;
    bool bMqInit = false;
    bool bWarned = false;
    bool bCheckConnection = false;
    bool mqttConnected = false;
    ustd::timeout mqttTickerTimeout = 5000L;

  public:
    Mqtt() {
        /*! Instantiate an MQTT client object using the PubSubClient library.
         *
         * This object connects to an external MQTT server as soon as a network
         * connection is available.
         *
         */
        mqttClient = wifiClient;
    }

    ~Mqtt() {
    }

    void begin(Scheduler *_pSched, String _mqttServer = "", uint16_t _mqttServerPort = 1883,
               bool _mqttRetained = false, String _clientName = "${hostname}",
               String _domainToken = "mu", String _outDomainToken = "omu",
               String _mqttUsername = "", String _mqttPassword = "", String _willTopic = "",
               String _willMessage = "") {
        /*! Connect to external MQTT server as soon as network is available
         *
         * This method starts the MQTT gateway using the information stored into the configuration
         * file /mqtt.json. All parameters passed in this method act as defaults if the
         * corresponding option is not set in the configuration file. As soon as a network
         * connection is available, the MQTT gateway is started.
         *
         * @param _pSched Pointer to the muwerk scheduler.
         * @param _mqttServer (optional, default is empty) Hostname or ip address of the MQTT
         * server.
         * @param _mqttServerPort (optional, default is 1883) Port number under which the MQTT
         * server is reachable
         * @param _mqttRetained (optional, default is `false`) If `true`, all messages published to
         * the MQTT server will be flagged as RETAINED.
         * @param _clientName (optional, default is ${hostname}) the MQTT client name. **WARNING:**
         * this name must be unique! Otherwise the MQTT server will rapidly disconnect.
         * @param _domainToken (optional, default is "mu") The MQTT client submitts to message
         * topics `<_clientName>/#` and `<_domainToken>/#`, strips both `<_domainToken>` and
         * `<_clientName>` from received topics and publishes those messages to the internal muwerk
         * interface `ustd::scheduler.publish()`.
         * @param _outDomainToken (optional, default is "omu") All pubblications from this client to
         * outside MQTT-servers have their topic prefixed by `<outDomainName>/<clientName>/topic`.
         * This is to prevent recursions.
         * @param _mqttUsername Username for mqtt server authentication, leave empty "" for no
         * username.
         * @param _mqttPassword Password for mqtt server authentication, leave empty "" for no
         * password.
         * @param _willTopic Topic of mqtt last will. Default is
         * `<outDomainName>/<clientName>/mqtt/state`. Note: it is not recommended to change
         * will-configuration, when using the home-assistant configuration.
         * @param _willMessage Message content for last will message. Default is `disconnected`
         *
         * he configuration options `clientName` and `willMessage` support the use of placeholders
         * in order to allow values that are specific to a certain device without the need to create
         * separate configuration files. Placeholders are written in the form of `${PLACEHOLDER}`.
         *
         * The following placeholders are currently available:
         * * `mac`: full mac address
         * * `macls`: last 4 digits of mac address
         * * `macfs`: first 4 digits of mac address
         * * `hostname`: the hostname of the system (from network)
         */
        ustd::jsonfile conf;

        // read configuration
        mqttServer = conf.readString("mqtt/host", _mqttServer);
        mqttServerPort = (uint16_t)conf.readLong("mqtt/port", 1, 65535, _mqttServerPort);
        mqttUsername = conf.readString("mqtt/username", _mqttUsername);
        mqttPassword = conf.readString("mqtt/password", _mqttPassword);
        mqttRetained = conf.readBool("mqtt/alwaysRetained", _mqttRetained);
        clientName = conf.readString("mqtt/clientName", 1, _clientName);
        domainToken = conf.readString("mqtt/domainToken", 1, _domainToken);
        outDomainToken = conf.readString("mqtt/outDomainToken", _outDomainToken);
        lwTopic = conf.readString("mqtt/lastWillTopic", _willTopic);
        lwMsg = conf.readString("mqtt/lastWillMessage", _willMessage);

        // persistently initialized tables
        conf.readStringArray("mqtt/subscriptions", subsList);
        conf.readStringArray("mqtt/outgoingBlackList", outgoingBlockList);
        conf.readStringArray("mqtt/incomingBlackList", incomingBlockList);

        // This configuration is preliminary but it is ok. Currently we have no network connection
        // and nothing can happen with this prelimiary information. As soon as a network connection
        // is established, the configuration information will be finalized. This is not possible now
        // since the replacement of placeholders must be able to access some network stack
        // information like the mac id or the hostname but this information is inaccesible if the
        // network stack has not been enabled and configured.

        // init scheduler
        pSched = _pSched;
        tID = pSched->add([this]() { this->loop(); }, "mqtt");

        // subscribe to all messages
        pSched->subscribe(tID, "#", [this](String topic, String msg, String originator) {
            this->subsMsg(topic, msg, originator);
        });

        if (mqttServer.length()) {
            // query update from network stack
            pSched->publish("net/network/get");
        } else {
            DBG("mqtt: WARNING - no server defined.");
        }

        // initialize runtime
        isOn = true;
        netUp = false;
        bMqInit = configureMqttClient();
        bWarned = false;
        bStateRetained = false;
        bCheckConnection = false;
        mqttConnected = false;
        mqttTickerTimeout = 5000L;  // 5 seconds

        publishState();
    }

    int addSubscription(int taskID, String topic, T_SUBS subs, String originator = "") {
        /*! Subscribe via MQTT server to a topic to receive messages published to this topic
         *
         * This function is similar to muwerk's `subscribe()` function, but in addition, this
         * function does an external MQTT subscribe. By default, munet's mqtt only subscribes to
         * topics that either start with `clientName` or with an optional `domainName`. Via this
         * function, arbitrary MQTT subscriptions can be added.
         *
         * `addSubscription()` subscribes on two layers: locally to muwerk's scheduler, and
         * externally with the MQTT server.
         *
         * @param taskID ID of the task that is associated with this subscriptions (only used for
         * statistics)
         * @param topic MQTT-style topic to be subscribed, can contain MQTT wildcards '#' and '*'.
         * (A subscription to '#' receives all pubs)
         * @param subs Callback of type `void myCallback(String topic, String msg, String
         * originator)` that is called, if a matching message is received. On ESP or Unixoid
         * platforms, this can be a member function.
         * @param originator Optional name of associated task.
         * @return subscriptionHandle on success (needed for unsubscribe), or -1 on error.
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
         * @param subscriptionHandle Handle to subscription as returned by `subscribe()`, used for
         * unsubscribe with muwerk's scheduler.
         * @param topic The topic string that was used in `addSubscription`, used for unsubscribe
         * via MQTT server.
         * @return `true` on successful unsubscription, `false` if no corresponding subscription is
         * found.
         */

        bool ret = pSched->unsubscribe(subscriptionHandle);
        for (unsigned int i = 0; i < subsList.length(); i++) {
            if (topic == subsList[i])
                subsList.erase(i);
        }
        return ret;
    }

    bool outgoingBlockSet(String topic) {
        /*! Block a topic-wildcard from being published to external mqtt server
         *
         * @param topic An mqtt topic wildcard for topics that should not be forwarded to external
         * mqtt. E.g. `mymupplet/#` Would block all messages a mupplet with name 'mymupplet'
         * publishes from being forwarded to the extern mqtt server
         * @return `true` on success or if entry already exists, `false` if entry couldn't be added.
         */
        for (unsigned int i = 0; i < outgoingBlockList.length(); i++) {
            if (outgoingBlockList[i] == topic)
                return true;
        }
        if (outgoingBlockList.add(topic) == -1)
            return false;
        return true;
    }

    bool outgoingBlockRemove(String topic) {
        /*! Unblock a topic-wildcard from being published to external mqtt server
         *
         * @param topic An mqtt topic wildcard for topics that should again be forwarded to external
         * mqtt. Unblock only removes a a block identical to the given topic. So topic must be
         * identical to a topic (wildcard) that has been used with `outgoingBlockSet()`.
         * @return `true` on success, `false` if no corresponding block could be found.
         */
        for (unsigned int i = 0; i < outgoingBlockList.length(); i++) {
            if (outgoingBlockList[i] == topic) {
                if (!outgoingBlockList.erase(i))
                    return false;
                return true;
            }
        }
        return false;
    }

    bool incomingBlockSet(String topic) {
        /*! Block a topic-wildcard from being published to the internal scheduler
         *
         * @param topic An mqtt topic wildcard for topics that should not be forwarded from external
         * mqtt server to the muwerk scheduler. This can be used to block any incoming messages
         * according to their topic.
         * @return `true` on success or if entry already exists, `false` if entry couldn't be added.
         */
        for (unsigned int i = 0; i < incomingBlockList.length(); i++) {
            if (incomingBlockList[i] == topic)
                return true;
        }
        if (incomingBlockList.add(topic) == -1)
            return false;
        return true;
    }

    bool incomingBlockRemove(String topic) {
        /*! Unblock a topic-wildcard from being received from external mqtt server
         *
         * @param topic An mqtt topic wildcard for topics that should again be forwarded internally
         * to muwerk. Unblock only removes a a block identical to the given topic. So topic must be
         * identical to a topic (wildcard) that has been used with `incomingBlockSet()`.
         * @return `true` on success, `false` if no corresponding block entry be found.
         */
        for (unsigned int i = 0; i < incomingBlockList.length(); i++) {
            if (incomingBlockList[i] == topic) {
                if (!incomingBlockList.erase(i))
                    return false;
                return true;
            }
        }
        return false;
    }

  private:
    inline void publishState() {
        pSched->publish("mqtt/state", mqttConnected ? "connected" : "disconnected");
    }

    void loop() {
        if (!isOn || !netUp || mqttServer.length() == 0) {
            return;
        }
        if (mqttConnected) {
            mqttClient.loop();
        }
        if (bCheckConnection || mqttTickerTimeout.test()) {
            mqttTickerTimeout.reset();
            bCheckConnection = false;
            if (!mqttClient.connected()) {
                // Attempt to connect
                const char *usr = mqttUsername.length() ? mqttUsername.c_str() : NULL;
                const char *pwd = mqttPassword.length() ? mqttPassword.c_str() : NULL;
                bool conRes = mqttClient.connect(clientName.c_str(), usr, pwd, lwTopic.c_str(), 0,
                                                 true, lwMsg.c_str());
                if (conRes) {
                    DBG2("Connected to mqtt server");
                    mqttConnected = true;
                    mqttClient.subscribe((clientName + "/#").c_str());
                    mqttClient.subscribe((domainToken + "/#").c_str());
                    for (unsigned int i = 0; i < subsList.length(); i++) {
                        mqttClient.subscribe(subsList[i].c_str());
                    }
                    bWarned = false;
                    pSched->publish("mqtt/config", outDomainPrefix + "+" + lwTopic + "+" + lwMsg);
                    publishState();
                } else {
                    mqttConnected = false;
                    if (!bWarned) {
                        bWarned = true;
                        publishState();
                        DBG2("MQTT disconnected.");
                    }
                }
            }
        }
    }

    void mqttReceive(char *ctopic, unsigned char *payload, unsigned int length) {
        String msg;
        String topic;

        // prepare message and topic
        topic = (const char *)ctopic;
        if (length && payload) {
            char *szBuffer = (char *)malloc(length + 1);
            if (szBuffer) {
                memcpy(szBuffer, payload, length);
                szBuffer[length] = 0;
                msg = szBuffer;
                free(szBuffer);
            } else {
                DBG("mqtt: ERROR - message body lost due to memory outage");
            }
        }

        for (unsigned int i = 0; i < incomingBlockList.length(); i++) {
            if (Scheduler::mqttmatch(topic, incomingBlockList[i])) {
                // blocked incoming
                DBG2("mqtt: Blocked " + topic);
                return;
            }
        }
        for (unsigned int i = 0; i < subsList.length(); i++) {
            if (Scheduler::mqttmatch(topic, subsList[i])) {
                DBG2("mqtt: subscribed topic " + topic);
                pSched->publish(topic, msg, "mqtt");
                return;
            }
        }
        // strip the client name token or the domain token in messages for us
        for (unsigned int i = 0; i < ownedPrefixes.length(); i++) {
            if (ownedPrefixes[i].length() <= topic.length()) {
                // basically this comparison is not really needed since at this point we could
                // ONLY have messages that match either the domainToken or the clientName since
                // we have exactly subscribed to those. But who knows....
                if (ownedPrefixes[i] == topic.substring(0, ownedPrefixes[i].length())) {
                    topic = (const char *)(ctopic + ownedPrefixes[i].length());
                    pSched->publish(topic, msg, "mqtt");
                }
            }
        }
    }

    void subsMsg(String topic, String msg, String originator) {
        if (originator == "mqtt") {
            return;  // avoid loops
        }

        // router function
        if (mqttConnected) {
            unsigned int len = msg.length() + 1;
            for (unsigned int i = 0; i < outgoingBlockList.length(); i++) {
                if (Scheduler::mqttmatch(topic, outgoingBlockList[i])) {
                    // Item is blocked.
                    return;
                }
            }
            String tpc;
            if (topic.c_str()[0] == '!') {
                tpc = &(topic.c_str()[1]);
            } else {
                tpc = outDomainPrefix + "/" + topic;
            }

            bool bRetain = mqttRetained;
            if (tpc.c_str()[0] == '!') {
                // remove second exclamation point
                tpc = &(topic.c_str()[2]);
                bRetain = true;
            }
            if (!bRetain && bStateRetained && topic == "mqtt/state") {
                // the state topic shall always be retained
                bRetain = true;
            }

            DBG3("mqtt: publishing...");
            if (mqttClient.publish(tpc.c_str(), msg.c_str(), bRetain)) {
                DBG2("mqtt publish: " + topic + " | " + msg);
            } else {
                DBG("mqtt: ERROR len=" + String(len) + ", not published: " + topic + " | " + msg);
                if (len > 128) {
                    DBG("mqtt: FATAL ERROR: you need to re-compile the PubSubClient library "
                        "and "
                        "increase #define MQTT_MAX_PACKET_SIZE.");
                }
            }
        } else {
            DBG2("mqtt: NO CONNECTION, not published: " + topic + " | " + msg);
        }

        // internal processing
        if (topic == "mqtt/state/get") {
            publishState();
        } else if (topic == "mqtt/config/get") {
            pSched->publish("mqtt/config", outDomainPrefix + "+" + lwTopic + "+" + lwMsg);
        } else if (topic == "mqtt/outgoingblock/set") {
            outgoingBlockSet(msg);
        } else if (topic == "mqtt/outgoingblock/remove") {
            outgoingBlockRemove(msg);
        } else if (topic == "mqtt/incomingblock/set") {
            incomingBlockSet(msg);
        } else if (topic == "mqtt/incomingblock/remove") {
            incomingBlockRemove(msg);
        } else if (topic == "net/network") {
            // network state received:
            JSONVar jsonState = JSON.parse(msg);
            if (JSON.typeof(jsonState) != "object") {
                DBG("mqtt: Received broken network state " + msg);
                return;
            }
            String state = (const char *)jsonState["state"];
            String hostname = (const char *)jsonState["hostname"];
            String mac = (const char *)jsonState["mac"];
            if (state == "connected") {
                DBG3("mqtt: received network connect");
                if (!netUp) {
                    DBG2("mqtt: net state online");
                    finalizeConfiguration(hostname, mac);
                    netUp = true;
                    bCheckConnection = true;
                }
            } else {
                netUp = false;
                mqttConnected = false;
                publishState();
                DBG2("mqtt: net state offline");
            }
        }
    }

    bool configureMqttClient() {
        if (mqttServer.length() == 0) {
            DBG2("mqtt: No mqtt host defined. Ignoring configuration...");
            return false;
        }
        bCheckConnection = true;
        mqttClient.setServer(mqttServer.c_str(), mqttServerPort);

        if (!bMqInit) {
            mqttClient.setCallback([this](char *topic, unsigned char *msg, unsigned int len) {
                this->mqttReceive(topic, msg, len);
            });
            bMqInit = true;
        }
        return true;
    }

    void finalizeConfiguration(String &hostname, String &mac) {
        // get network information
        if (hostname.length() == 0) {
#if defined(__ESP32__)
            String hostname = WiFi.getHostname();
#else
            String hostname = WiFi.hostname();
#endif
        }
        if (mac.length() == 0) {
            mac = WiFi.macAddress();
        }
        mac.replace(":", "");

        // transform and integrate missing configuration data
        clientName = replaceVars(clientName, hostname, mac);
        if (outDomainToken.length()) {
            outDomainPrefix = outDomainToken + "/" + clientName;
        } else {
            outDomainPrefix = clientName;
        }
        if (lwTopic.length()) {
            lwMsg = replaceVars(lwMsg, hostname, mac);
        } else {
            lwTopic = outDomainPrefix + "/mqtt/state";
            lwMsg = "disconnected";
            bStateRetained = true;
        }
        String clientPrefix = clientName + "/";
        String domainPrefix = domainToken + "/";
        ownedPrefixes.erase();
        ownedPrefixes.add(clientPrefix);
        ownedPrefixes.add(domainPrefix);
    }

    String replaceVars(String val, String &hostname, String &macAddress) {
        val.replace("${hostname}", hostname);
        val.replace("${mac}", macAddress);
        val.replace("${macls}", macAddress.substring(6));
        val.replace("${macfs}", macAddress.substring(0, 5));
        return val;
    }
};

}  // namespace ustd

// #endif  // defined(__ESP__)
