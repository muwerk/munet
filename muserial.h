// muserial.h
#pragma once

#include "platform.h"
#include "array.h"
#include "map.h"

#include "scheduler.h"
//#include <Arduino_JSON.h>

#ifdef __ATTINY__
#define HardwareSerial TinySoftwareSerial
#endif

namespace ustd {

/*! \brief munet MuSerial Class

The MuSerial class connects two muwerk MCUs via a serial connection.
The serial connection automatically fowards all pub/sub messages (that are not
blocked by exception lists) between the two nodes.

A main application could be to connect a non-networked MCU to a networked
MCU to allow forwarding and receiving MQTT messages on MCUs without network
connection via a serial link.

A system of two muwerk MCUs connected via MuSerial act to the outside world
as if they were one system. Hardware (mupplets) can be addressed the same
way, regardless if they are on node1 or node2.

## Sample MuSerial node (without network acces)

\code{cpp}
#define __ESP__ 1   // Platform defines required, see doc, mainpage.
#include "scheduler.h"
#include "muserial.h"

ustd::Scheduler sched;
ustd::MuSerial serlink("serlink", &Serial2, 115200, LED_BUILTIN);

void setup() {
    muserial.begin(&sched);
}
\endcode

## Sample MuSerial node (with network access)

\code{cpp}
#include "scheduler.h"

#include "muserial.h"

#include "net.h"
#include "mqtt.h"
#include "ota.h"

ustd::Scheduler sched(10, 16, 32);
ustd::MuSerial muser("esp32", &Serial1, 115200, LED_BUILTIN);

ustd::Net net(LED_BUILTIN);
ustd::Mqtt mqtt;
ustd::Ota ota;

void setup() {
    muser.begin(&sched);
    net.begin(&sched);
    mqtt.begin(&sched);
    ota.begin(&sched);
}
\endcode

For a complete example, see:
<a href="https://github.com/muwerk/examples/tree/master/serialBridge">muwerk SerialBridge
example</a>
*/
class MuSerial {
  private:
    Scheduler *pSched;
    int tID;

    String name;
    HardwareSerial *pSerial;
    unsigned long baudRate;
    uint8_t connectionLed;
    unsigned long ledTimer = 0;

    enum LinkState { SYNC, HEADER, MSG, CRC };
    bool bCheckLink = false;
    uint8_t blockNum = 0;
    LinkState linkState;
    unsigned long lastRead = 0;
    unsigned long lastMsg = 0;
    unsigned long lastPingSent = 0;
    bool linkConnected = false;
    unsigned long readTimeout = 5;          // sec
    unsigned long pingReceiveTimeout = 10;  // sec
    unsigned long pingPeriod = 5;           // sec
    String remoteName = "";
    ustd::array<String> outgoingBlockList;
    ustd::array<String> incomingBlockList;

    const uint8_t SOH = 0x01, STX = 0x02, ETX = 0x03, EOT = 0x04;
    const uint8_t VER = 0x01;

    /*! \brief protocol elements of MuSerial */
    enum LinkCmd {
        MUPING,  //!< period ping messages consisting of
                 //!< <unix-time-as-string><nul><remote-system-name><nul>
        MQTT     //!< MQTT message consisting of: <topic><nul><message><nul>
    };

    /*! \brief Header of serial transmission

    MuSerial sends messages as <Header><payload><Footer>
    */
    typedef struct t_header {
        uint8_t soh;   //!< = SOH;
        uint8_t ver;   //!< = VER;  first byte included in CRC calculation
        uint8_t num;   //!< block number
        uint8_t cmd;   //!< \ref LinkCmd
        uint8_t hLen;  //!< Hi byte payload length
        uint8_t lLen;  //!< Lo byte length, payload length is hLen*256+lLen
        uint8_t stx;   //!< = STX;
        uint8_t pad;   //!< = 0;  (padding)
    } T_HEADER;

    /*! \brief Footer of serial transmission */
    typedef struct t_footer {
        uint8_t etx;   //!< = ETX;  Last byte included in CRC calculation
        uint8_t pad2;  //!< = 0; (padding)
        uint8_t crc;   //!< primite CRC, calculated starting with ver-field of header, payload and
                       //!< footer up and including etx.
        uint8_t eot;   //!< = EOT;
    } T_FOOTER;

  public:
    bool activeLogic = false;  //!< If a connectionLed is used, this defines if active-high (true)
                               //!< or active-low (false) logic is used.
    unsigned long connectionLedBlinkDurationMs =
        200;  //!< milli-secs the connectionLed is flashed on receiving a ping.

    MuSerial(String name, HardwareSerial *pSerial, unsigned long baudRate = 115200,
             uint8_t connectionLed = -1)
        : name(name), pSerial(pSerial), baudRate(baudRate), connectionLed(connectionLed) {
        /*! Instantiate a serial link between two muwerk instances.

        @param name Name of this node (used in pub/sub protocol, received as 'remoteName' by other
        system)
        @param pSerial pointer to Serial object
        @param baudRate baud rate for communication. Must be same as used by other node.
        @param connectionLed optional gpio pin number of a led (e.g. LED_BUILTIN) that is flashed on
        receiving a PING from other system.
         */
    }

    ~MuSerial() {
    }

    void begin(Scheduler *_pSched) {
        /*! Setup serial link.
         *
         * @param _pSched Pointer to the muwerk scheduler.
         */
        pSched = _pSched;
        pSerial->begin(baudRate);
#ifdef __ARDUINO__
        while (!*pSerial) {
        }
#endif

        auto ft = [=]() { this->loop(); };
        tID = pSched->add(ft, "serlink", 20000L);  // check every 20ms
        auto fnall = [=](String topic, String msg, String originator) {
            this->subsMsg(topic, msg, originator);
        };
        pSched->subscribe(tID, "#", fnall);
        bCheckLink = true;
        linkState = SYNC;
        if (connectionLed != -1) {
            pinMode(connectionLed, OUTPUT);
            digitalWrite(connectionLed, activeLogic);
        }
        ping();
    }

    bool outgoingBlockSet(String topic) {
        /*! Block a topic-wildcard from being published to external mqtt server
         *
         * @param topic An mqtt topic wildcard for topics that should not be
         * forwarded to external mqtt. E.g. 'mymupplet/#' Would block all messages
         * a mupplet with name 'mymupplet' publishes from being forwarded to the
         * extern mqtt server
         * @return true on success, false if entry already exists, or couldn't be added.
         */
        for (unsigned int i = 0; i < outgoingBlockList.length(); i++) {
            if (outgoingBlockList[i] == topic)
                return false;
        }
        if (outgoingBlockList.add(topic) == -1)
            return false;
        return true;
    }

    bool outgoingBlockRemove(String topic) {
        /*! Unblock a topic-wildcard from being published to external mqtt server
         *
         * @param topic An mqtt topic wildcard for topics that should again be
         * forwarded to external mqtt. Unblock only removes a a block identical to
         * the given topic. So topic must be identical to a topic (wildcard) that
         * has been used with 'outgoingBlockSet()'.
         * @return true on success, false if no corresponding block could be found.
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
         * @param topic An mqtt topic wildcard for topics that should not be
         * forwarded from external mqtt server to the muwerk scheduler. This can be
         * used to block any incoming messages according to their topic.
         * @return true on success, false if entry already exists, or couldn't be added.
         */
        for (unsigned int i = 0; i < incomingBlockList.length(); i++) {
            if (incomingBlockList[i] == topic)
                return false;
        }
        if (incomingBlockList.add(topic) == -1)
            return false;
        return true;
    }

    bool incomingBlockRemove(String topic) {
        /*! Unblock a topic-wildcard from being received from external mqtt server
         *
         * @param topic An mqtt topic wildcard for topics that should again be
         * forwarded internally to muwerk. Unblock only removes a a block identical to
         * the given topic. So topic must be identical to a topic (wildcard) that
         * has been used with 'incomingBlockSet()'.
         * @return true on success, false if no corresponding block could be found.
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
    unsigned char crc(const unsigned char *buf, unsigned int len, unsigned char init = 0) {
        unsigned char c = init;
        for (unsigned int i = 0; i < len; i++)
            c = c ^ buf[i];
        return c;
    }

    void ping() {
        char strTime[16];
#ifdef __ARDUINO__
        ltoa(pSched->getUptime(), strTime, 15);
#else
        ltoa(time(nullptr), strTime, 15);
#endif
        sendOut(strTime, name, LinkCmd::MUPING);
        lastPingSent = pSched->getUptime();
    }

    void handleTime(uint64_t remoteTime) {
        // XXX do maybe something?
    }

    void sendOut(String topic, String msg, LinkCmd cmd = LinkCmd::MQTT) {
        T_HEADER th = {};
        T_FOOTER tf = {};
        unsigned char ccrc;
        unsigned char nul = 0x0;

        // Serial.println("Sending " + topic + ", " + msg);
        th.soh = SOH;
        th.ver = VER;
        th.num = blockNum++;
        th.cmd = cmd;
        unsigned int len = topic.length() + msg.length() + 2;
        th.hLen = len / 256;
        th.lLen = len % 256;
        th.stx = STX;

        tf.etx = ETX;
        tf.eot = EOT;

        ccrc = crc((const uint8_t *)&(th.ver), sizeof(th) - 1);
        ccrc = crc((const uint8_t *)topic.c_str(), topic.length(), ccrc);
        ccrc = crc((const uint8_t *)&nul, 1, ccrc);
        ccrc = crc((const uint8_t *)msg.c_str(), msg.length(), ccrc);
        ccrc = crc((const uint8_t *)&nul, 1, ccrc);
        ccrc = crc((const uint8_t *)&tf, 2, ccrc);

        tf.crc = ccrc;

        pSerial->write((unsigned char *)&th, sizeof(th));
        pSerial->write((unsigned char *)topic.c_str(), topic.length());
        pSerial->write((uint8_t)nul);
        pSerial->write((unsigned char *)msg.c_str(), msg.length());
        pSerial->write((uint8_t)nul);
        pSerial->write((unsigned char *)&tf, sizeof(tf));
    }

  private:
    T_HEADER hd;
    unsigned char *pHd;
    uint16_t hLen;
    uint16_t msgLen, curMsg;
    unsigned char *msgBuf = nullptr;
    bool allocated = false;
    T_FOOTER fo;
    unsigned char *pFo;
    uint16_t cLen;

    bool internalPub(String topic, String msg) {
        for (unsigned int i = 0; i < incomingBlockList.length(); i++) {
            if (Scheduler::mqttmatch(topic, incomingBlockList[i])) {
                return false;
            }
        }

        // Serial.println("In: " + topic);
        String pre2 = remoteName + "/";
        String pre1 = name + "/";
        if (topic.substring(0, pre1.length()) == pre1) {
            topic = topic.substring(pre1.length());
        }
        if (topic.substring(0, pre2.length()) == pre1) {
            topic = topic.substring(pre2.length());
        }

        // Serial.println("InPub: " + topic);
        pSched->publish(topic, msg, remoteName);
        return true;
    }

    bool ld = false;
    void loop() {
        unsigned char ccrc;
        unsigned char c;
        if (bCheckLink) {
            if (ledTimer) {
                if (timeDiff(ledTimer, millis()) > connectionLedBlinkDurationMs) {
                    ledTimer = 0;
                    if (connectionLed != -1) {
                        digitalWrite(connectionLed, activeLogic);
                    }
                }
            }
            if (pSched->getUptime() - lastPingSent > pingPeriod) {
                ping();
            }
            while (pSerial->available() > 0) {
                c = pSerial->read();
                lastRead = pSched->getUptime();
                switch (linkState) {
                case SYNC:
                    if (c == SOH) {
                        hd.soh = SOH;
                        linkState = HEADER;
                        pHd = (unsigned char *)&hd;
                        hLen = 1;
                    }
                    continue;
                    break;
                case HEADER:
                    pHd[hLen] = c;
                    hLen++;
                    if (hLen == sizeof(hd)) {
                        // XXX: check block number
                        if (hd.ver != VER || hd.stx != STX) {
                            linkState = SYNC;
                        } else {
                            msgLen = 256 * hd.hLen + hd.lLen;
                            if (msgLen < 1024) {
                                msgBuf = (unsigned char *)malloc(msgLen);
                                curMsg = 0;
                                if (msgBuf) {
                                    linkState = MSG;
                                    allocated = true;
                                } else {
                                    linkState = SYNC;
                                }
                            } else {
                                linkState = SYNC;
                            }
                        }
                    }
                    continue;
                    break;
                case MSG:
                    msgBuf[curMsg] = c;
                    ++curMsg;
                    if (curMsg == msgLen) {
                        linkState = CRC;
                        pFo = (unsigned char *)&fo;
                        cLen = 0;
                    }
                    continue;
                    break;
                case CRC:
                    pFo[cLen] = c;
                    ++cLen;
                    if (cLen == sizeof(fo)) {
                        if (fo.etx != ETX || fo.eot != EOT) {
                            if (allocated && msgBuf != nullptr) {
                                free(msgBuf);
                                msgBuf = nullptr;
                                allocated = false;
                            }
                            linkState = SYNC;
                            continue;
                        } else {
                            ccrc = crc((const uint8_t *)&(hd.ver), sizeof(hd) - 1);
                            ccrc = crc(msgBuf, msgLen, ccrc);
                            ccrc = crc((const uint8_t *)&fo, 2, ccrc);
                            if (ccrc != fo.crc) {
                                if (allocated && msgBuf != nullptr) {
                                    free(msgBuf);
                                    msgBuf = nullptr;
                                    allocated = false;
                                }
                                linkState = SYNC;
                                continue;
                            } else {
                                uint64_t remoteTime = 0;
                                // Serial.println("Msg received");
                                if (strlen((const char *)msgBuf) + 2 <= msgLen) {
                                    lastMsg = pSched->getUptime();
                                    const char *pM =
                                        (const char *)&msgBuf[strlen((const char *)msgBuf) + 1];
                                    if (strlen(pM) + strlen((const char *)msgBuf) + 2 <= msgLen) {
                                        switch ((LinkCmd)hd.cmd) {
                                        case LinkCmd::MUPING:
                                            remoteName = pM;
                                            // XXX: Y2031?
                                            remoteTime = atol((const char *)msgBuf);
                                            handleTime(remoteTime);
                                            lastMsg = pSched->getUptime();
                                            if (connectionLed != -1) {
                                                digitalWrite(connectionLed, !activeLogic);
                                                ledTimer = millis();
                                            }
                                            if (!linkConnected) {
                                                linkConnected = true;
                                                pSched->publish(name + "/link/" + remoteName,
                                                                "connected", name);
                                            }
                                            break;
                                        case LinkCmd::MQTT:
                                            internalPub((const char *)msgBuf, pM);
                                            break;
                                        }
                                    }
                                }
                                if (allocated && msgBuf != nullptr) {
                                    free(msgBuf);
                                    msgBuf = nullptr;
                                    allocated = false;
                                }
                                linkState = SYNC;
                            }
                        }
                    }
                    continue;
                    break;
                }
            }
            if (linkConnected || linkState != SYNC) {
                if (linkState != SYNC) {
                    if ((unsigned long)(pSched->getUptime() - lastRead) > readTimeout) {
                        linkState = SYNC;
                        if (linkConnected) {
                            pSched->publish(name + "/link/" + remoteName, "disconnected", name);
                        }
                        linkConnected = false;
                        if (allocated && msgBuf != nullptr) {
                            free(msgBuf);
                            msgBuf = nullptr;
                            allocated = false;
                        }
                    }
                } else {
                    if ((unsigned long)(pSched->getUptime() - lastMsg) > pingReceiveTimeout) {
                        if (linkConnected) {
                            pSched->publish(name + "/link/" + remoteName, "disconnected", name);
                        }
                        linkConnected = false;
                    }
                }
            }
        }
    }

    void subsMsg(String topic, String msg, String originator) {
        if (originator == remoteName) {
            // prevent loops;
            // Serial.println("Loop prevented: " + topic + " - " + msg + " from: " + originator);
            return;
        }
        // Serial.println("MQ-in: " + topic + " - " + msg + " from: " + originator);
        for (unsigned int i = 0; i < outgoingBlockList.length(); i++) {
            if (Scheduler::mqttmatch(topic, outgoingBlockList[i])) {
                // Serial.println("blocked: " + topic + " - " + msg + " from: " + originator);
                return;
            }
        }
        String pre = remoteName + "/";
        if (topic.substring(0, pre.length()) == pre) {
            sendOut(topic, msg);
        } else {
            sendOut(remoteName + "/" + topic, msg);
        }
    };
};

}  // namespace ustd
