// indrajala.h

#pragma once
#define USE_SERIAL_DBG 1

#include <functional>

#include <Arduino_JSON.h>
// https://registry.platformio.org/libraries/links2004/WebSockets
// Package: links2004/WebSockets

#undef __AVR__
#include <WebSocketsClient.h>
#include "ustd_platform.h"
#include "ustd_array.h"
#include "ustd_map.h"

#include "scheduler.h"
#include "timeout.h"

#include "munet.h"

namespace ustd {

/*! \brief munet Indrajala Gateway Class

The muwerk scheduler implements pub/sub inter-task communication between muwerk tasks. Tasks can
subscribe to Indrajala formatted topics and publish messages. The Indrajala Class implements a gateway between
the muwerk scheduler's message queue and an external Indrajala server.

### Publishing to external server:

This object extends the internal communication to external Indrajala servers. All internal muwerk
messages are published to the external Indrajala server with prefix `<outDomainPrefix>/<hostname>/`.
E.g. if a muwerk task `ustd::Scheduler.publish('led/set', 'on')` and `clientName` of ESP is
`myhost`, an Indra_Event message with domain `'ind/myhost/led/set'` and msg `on` is sent to the
external server. Default `outDomainPrefix` is `'ind'`. In order to publish to an unmodified topic,
prefix the topic with `'!'`, then neither `outDomainPrefix` nor `clientName` are prepended. E.g.
publish to topic `'!system/urgent'` will cause an Indra_Event message sent to domain `'system/urgent'` with no
additional prefixes. Note: this can cause recursions.

### Subscribing from external server:

This object subscribes two wild-card topics on the external Indrajala server:

  1. `<clientName>/#`
  2. `<domainToken>/#`

`<clientName>` is by default the hostname of the device chip, but can be overwritten using
`_clientName` in `ustd::Indrajala.begin();`. `domainToken` is "ie" by default and can be overwritten
using `_domainToken` in `ustd::Indrajala.begin()`;

Received messsages are stripped of `clientName` or `domainToken` prefix and published into
`ustd::Scheduler.publish()`. That way external Indrajala messages are routed to any muwerk task that uses
the internal muwerk `ustd::Scheduler.subscribe()` mechanism, and all muwerk tasks can publish to
external Indrajala entities transparently.

Additionally, arbitrary topics can be subscribed to via `addSubscription()`. Topics that are added
via `addSubscription()` are transparently forwarded. Nothing is stripped, and it is user's
responsibility to prevent loops.

## Sample Indrajala Integration

\code{cpp}
#define __ESP__ 1   // Platform defines required, see doc, mainpage.
#include "scheduler.h"
#include "net.h"
#include "indrajala.h"

ustd::Scheduler sched;
ustd::Net net();
ustd::Indrajala indrajala();

void setup() {
    net.begin(&sched);
    indrajala.begin(&sched);
}
\endcode

*/
class Indrajala {
  private:
    // muwerk task management
    Scheduler *pSched;
    int tID;

    // net client
    WiFiClient wifiClient;

    // active configuration
    String indraServer;
    uint16_t indraServerPort;
    String authToken;

    String domainToken;
    String outDomainToken;
    String indraAuthToken;
    // computed configuration
    ustd::array<String> ownedPrefixes;
    String outDomainPrefix;  // outDomainToken + '/' + clientName, or just clientName, if
                             // outDomainToken==""

    // persistently initialized tables
    ustd::array<String> subsList;

    // runtime control - state management
    bool isOn = false;
    bool bNetUp = false;
    bool bIndraInit = false;
    bool bWarned = false;
    bool bCheckConnection = false;
    bool bIndraConnected = false;
    bool bIndraConnecting = false;
    ustd::timeout indraTickerTimeout = 5000L;
    ustd::timeout indraConTimeout = 5000L;
    ustd::timeout indraEcho = 5000L;
    WebSocketsClient webSocket;
    double serverTimeOffset = 0.0;
    ustd::sensorprocessor meanTimeOffset = ustd::sensorprocessor(20, 0, 0.0000001);  // 4, 600, 0.01);
  public:
    Indrajala() {
        /*! Instantiate an Indrajala client.
         *
         * This object connects to an external Indrajala server using Websockets as soon as a network
         * connection is available.
         *
         */
        // indraClient = wifiClient;
        // webSocketsClient = WebSocketsClient();
    }

    ~Indrajala() {
    }

    void begin(Scheduler *_pSched, String _indraServer = "", uint16_t _indraServerPort = 1883,
               String _domainToken = "ie", String _outDomainToken = "ind",
               String _indraAuthToken = "") {
        /*! Connect to external Indrajala server as soon as network is available
         *
         * This method starts the Indrajala gateway using the information stored into the configuration
         * file /indrajala.json. All parameters passed in this method act as defaults if the
         * corresponding option is not set in the configuration file. As soon as a network
         * connection is available, the Indrajala gateway is started.
         *
         * @param _pSched Pointer to the muwerk scheduler.
         * @param _indraServer (optional, default is empty) Hostname or ip address of the Indrajala
         * server.
         * @param _indraServerPort (optional, default is 1883) Port number under which the Indrajala
         * server is reachable
         * @param _domainToken (optional, default is "mu") The Indrajala client submitts to message
         * topics `<_clientName>/#` and `<_domainToken>/#`, strips both `<_domainToken>` and
         * `<_clientName>` from received topics and publishes those messages to the internal muwerk
         * interface `ustd::scheduler.publish()`.
         * @param _outDomainToken (optional, default is "omu") All pubblications from this client to
         * outside Indrajala-servers have their topic prefixed by `<outDomainName>/<clientName>/topic`.
         * This is to prevent recursions.
         * @param _indraAuthToken auth_token for indrajala server authentication, leave empty "" for no
         * authentication.
         */
        ustd::jsonfile conf;

        // read configuration
        indraServer = conf.readString("indrajala/host", _indraServer);
        indraServerPort = (uint16_t)conf.readLong("indrajala/port", 1, 65535, _indraServerPort);
        indraAuthToken = conf.readString("indrajala/auth_token", _indraAuthToken);
        domainToken = conf.readString("indrajala/domain_token", 1, _domainToken);
        outDomainToken = conf.readString("indrajala/out_domain_token", _outDomainToken);

        // persistently initialized tables
        conf.readStringArray("indrajala/subscriptions", subsList);

        // init scheduler
        pSched = _pSched;
        tID = pSched->add([this]() { this->loop(); }, "indra", 1000L);

        // subscribe to all messages
        pSched->subscribe(tID, "#", [this](String topic, String msg, String originator) {
            this->subsMsg(topic, msg, originator);
        });

        if (indraServer.length()) {
            // query update from network stack
            pSched->publish("net/network/get");
        } else {
            DBG("indra: WARNING - no server defined.");
        }

        // initialize runtime
        isOn = true;
        bNetUp = false;
        bIndraInit = true;
        bWarned = false;
        bCheckConnection = false;
        bIndraConnected = false;
        bIndraConnecting = false;
        indraConTimeout = 5000L;     // 5 seconds
        indraTickerTimeout = 5000L;  // 5 seconds
        indraEcho = 5000L;           // 5 seconds

        publishState();
    }

    bool indra_connect() {
        return false;
    }

    bool indra_subscribe(String topic) {
        return false;
    }

    bool indra_publish(String topic, String message) {
        return false;
    }

    int addSubscription(int taskID, String topic, T_SUBS subs, String originator = "") {
        /*! Subscribe via Indrajala server to a topic to receive messages published to this topic
         *
         * This function is similar to muwerk's `subscribe()` function, but in addition, this
         * function does an external Indrajala subscribe. By default, munet's indra only subscribes to
         * topics that either start with `clientName` or with an optional `domainName`. Via this
         * function, arbitrary Indrajala subscriptions can be added.
         *
         * `addSubscription()` subscribes on two layers: locally to muwerk's scheduler, and
         * externally with the Indrajala server.
         *
         * @param taskID ID of the task that is associated with this subscriptions (only used for
         * statistics)
         * @param topic Indrajala-style topic to be subscribed, can contain Indrajala wildcards '#' and '*'.
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
                return handle;  // Already subbed via indra.
        }
        if (bIndraConnected) {
            indra_subscribe(topic.c_str());
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
         * via Indrajala server.
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

  private:
    inline void publishState() {
        pSched->publish("indrajala/state", bIndraConnected ? "connected" : "disconnected");
    }

    void hexdump(const void *mem, uint32_t len, uint8_t cols = 16) {
        const uint8_t *src = (const uint8_t *)mem;
        Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
        for (uint32_t i = 0; i < len; i++) {
            if (i % cols == 0) {
                Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
            }
            Serial.printf("%02X ", *src);
            src++;
        }
        Serial.printf("\n");
    }

    uint8_t rnd() {
        return (uint8_t)random(256);
    }

    String uuid4() {
        // https://gist.github.com/jed/982883
        String uuid = "";
        for (int i = 0; i < 36; i++) {
            if (i == 8 || i == 13 || i == 18 || i == 23) {
                uuid += "-";
            } else if (i == 14) {
                uuid += "4";
            } else if (i == 19) {
                uuid += "89ab"[rnd() % 4];
            } else {
                uuid += "0123456789abcdef"[rnd() % 16];
            }
        }
        return uuid;
    }

    double jd_time() {
        double dts = time(nullptr) + (millis() % 1000) / 1000.0 + serverTimeOffset;
        return dts / 86400.0 + 2440587.5;
    }

    void createIndraEvent(JSONVar *pIE, String domain, String data_type = "") {
        (*pIE)["domain"] = domain;
        (*pIE)["from_id"] = "muWerk/test";
        (*pIE)["uuid4"] = uuid4();
        (*pIE)["to_scope"] = "";
        (*pIE)["data_type"] = data_type;
        (*pIE)["time_jd_start"] = jd_time();
    }

    void sendEcho() {
        JSONVar indraEvent;
        String msg;
        createIndraEvent(&indraEvent, "$trx/echo", "json");
        indraEvent["data"] = "";
        msg = JSON.stringify(indraEvent);
        webSocket.sendTXT(msg.c_str());
    }

    void webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
        String domain, data, from_id, data_type;
        JSONVar indraEvent;
        double jd_now, jd_start, jd_end, jd_rtt, jd_dt1, jd_dt2;
        switch (type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WSc] Disconnected!\r\n");
            bIndraConnected = false;
            bIndraConnecting = false;
            bCheckConnection = true;
            publishState();
            break;
        case WStype_CONNECTED:
            Serial.printf("[WSc] Connected to url: %s\r\n", payload);
            bIndraConnected = true;
            bIndraConnecting = false;
            bCheckConnection = false;
            publishState();
            sendEcho();
            break;

        case WStype_TEXT:
            Serial.printf("[WSc] get text: %s\r\n", payload);
            indraEvent = JSON.parse((char *)payload);
            if (JSON.typeof(indraEvent) == "undefined") {
                Serial.printf("[WSc] parseObject() failed\r\n");
                return;
            } else {
                if (indraEvent.hasOwnProperty("domain") && indraEvent.hasOwnProperty("time_jd_start") &&
                    indraEvent.hasOwnProperty("data") && indraEvent.hasOwnProperty("from_id") &&
                    indraEvent.hasOwnProperty("uuid4") && indraEvent.hasOwnProperty("to_scope") &&
                    indraEvent.hasOwnProperty("data_type")) {
                    domain = (const char *)indraEvent["domain"];
                    from_id = (const char *)indraEvent["from_id"];
                    if (from_id == String("$trx/echo")) {
                        // echo received
                        jd_now = jd_time() * 86400;
                        jd_start = (double)indraEvent["time_jd_start"] * 86400;
                        jd_end = (double)indraEvent["time_jd_end"] * 86400;
                        jd_rtt = jd_now - jd_start;
                        jd_dt1 = jd_start - jd_end;
                        jd_dt2 = jd_now - jd_end;
                        // serverTimeOffset = (jd_dt1 + jd_dt2) / 2.0 - jd_rtt / 2.0;
                        serverTimeOffset = serverTimeOffset + jd_end - (jd_now + jd_start) / 2.0;
                        meanTimeOffset.filter(&serverTimeOffset);
                        Serial.printf("[WSc] echo received %lf, %lf, %lf, delta_t: %lf\r\n", jd_rtt, jd_dt1, jd_dt2, serverTimeOffset);
                    } else {
                        data = (const char *)indraEvent["data"];
                        pSched->publish(domain, data);
                    }
                }
            }
            // send message to server
            // webSocket.sendTXT("message here");
            break;
        case WStype_BIN:
            Serial.printf("[WSc] get binary length: %u\r\n", length);
            hexdump(payload, length);

            // send data to server
            // webSocket.sendBIN(payload, length);
            break;
        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
        }
    }

    void loop() {
        if (!isOn || !bNetUp || indraServer.length() == 0) {
            return;
        }
        if (bIndraConnected || bIndraConnecting) {
            // check for incoming messages
            webSocket.loop();
        }
        if (bIndraConnected) {
            if (indraEcho.test()) {
                indraEcho.reset();
                sendEcho();
            }
        }
        if (bCheckConnection || indraTickerTimeout.test()) {
            indraTickerTimeout.reset();
            bCheckConnection = false;
            if (!bIndraConnected) {
                if (!bIndraConnecting) {
                    Serial.println("Connecting to indrajala server.");
                    indraConTimeout.reset();
                    bIndraConnecting = true;
                    webSocket.begin(indraServer, indraServerPort, "/");
                    webSocket.onEvent([this](WStype_t type, uint8_t *payload, size_t length) { this->webSocketEvent(type, payload, length); });
                } else {
                    if (indraConTimeout.test()) {
                        Serial.printf("Indrajala connection timeout.");
                        bIndraConnecting = false;
                        bWarned = false;
                        publishState();
                    } else {
                        /*
                    // Attempt to connect
                    bool conRes = indra_connect();
                    if (conRes) {
                        DBG2("Connected to indrajala server");
                        bIndraConnected = true;
                        indra_subscribe((domainToken + "/#").c_str());
                        for (unsigned int i = 0; i < subsList.length(); i++) {
                            indra_subscribe(subsList[i].c_str());
                        }
                        bWarned = false;
                        pSched->publish("indrajala/config", outDomainPrefix);
                        publishState();
                    } else {
                        bIndraConnected = false;
                        if (!bWarned) {
                            bWarned = true;
                            publishState();
                            DBG2("Indrajala disconnected.");
                        }
                    }
                    */
                    }
                }
            }
        }
    }

    void indra_receive(char *ctopic, unsigned char *payload, unsigned int length) {
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
                DBG("indra: ERROR - message body lost due to memory outage");
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
                    pSched->publish(topic, msg, "indra");
                }
            }
        }
    }

    void subsMsg(String topic, String msg, String originator) {
        if (originator == "indra") {
            return;  // avoid loops
        }

        // router function
        if (bIndraConnected) {
            unsigned int len = msg.length() + 1;
            String tpc;
            if (topic.c_str()[0] == '!') {
                tpc = &(topic.c_str()[1]);
            } else {
                tpc = outDomainPrefix + "/" + topic;
            }

            if (tpc.c_str()[0] == '!') {
                // remove second exclamation point
                tpc = &(topic.c_str()[2]);
            }
            DBG3("indra: publishing...");
            if (indra_publish(tpc, msg)) {
                DBG2("indra publish: " + topic + " | " + msg);
            } else {
                DBG("indra: ERROR len=" + String(len) + ", not published: " + topic + " | " + msg);
            }
        } else {
            DBG2("indra: NO CONNECTION, not published: " + topic + " | " + msg);
        }

        // internal processing
        if (topic == "indrajala/state/get") {
            publishState();
        } else if (topic == "indrajala/config/get") {
            pSched->publish("indrajala/config", outDomainPrefix);
        } else if (topic == "net/network") {
            // network state received:
            JSONVar jsonState = JSON.parse(msg);
            if (JSON.typeof(jsonState) != "object") {
                DBG("indra: Received broken network state " + msg);
                return;
            }
            String state = (const char *)jsonState["state"];
            String hostname = (const char *)jsonState["hostname"];
            String mac = (const char *)jsonState["mac"];
            if (state == "connected") {
                Serial.println("indra: received network connect");
                if (!bNetUp) {
                    Serial.println("indra: net state online, checking indra connection...");
                    bNetUp = true;
                    bCheckConnection = true;
                }
            } else {
                bNetUp = false;
                bIndraConnected = false;
                publishState();
                DBG2("indra: net state offline");
            }
        }
    }
};

}  // namespace ustd

// #endif  // defined(__ESP__)
