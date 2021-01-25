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

class MuSerial {
  private:
    Scheduler *pSched;
    int tID;

    String name;
    HardwareSerial *pSerial;
    unsigned long baudRate;
    uint8_t connectionLed;

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
    String inDomainToken;
    String outDomainToken;
    ustd::array<String> outgoingBlockList;
    ustd::array<String> incomingBlockList;

    const uint8_t SOH = 0x01, STX = 0x02, ETX = 0x03, EOT = 0x04;
    const uint8_t VER = 0x01;

    enum LinkCmd { MUPING, MQTT };

    typedef struct t_header {
        uint8_t soh;   // = SOH;
        uint8_t ver;   // = VER;  // CRC start
        uint8_t num;   // block number
        uint8_t cmd;   // LinkCmd
        uint8_t hLen;  // = 0;  // Hi byte length
        uint8_t lLen;  // = 0;  // Lo byte length
        uint8_t stx;   // = STX;
        uint8_t pad;   // = 0;
    } T_HEADER;

    typedef struct t_footer {
        uint8_t etx;   // = ETX;  // CRC end
        uint8_t pad2;  // = 0;
        uint8_t crc;   // = 0;
        uint8_t eot;   // = EOT;
    } T_FOOTER;

    typedef struct t_ping {
        uint8_t soh;   // = SOH;
        uint8_t ver;   // = VER;  // CRC start
        uint8_t num;   // block number
        uint8_t cmd;   // LinkCmd
        uint8_t hLen;  // = 0;  // Hi byte length
        uint8_t lLen;  // = 0;  // Lo byte length
        uint8_t stx;   // = STX;
        uint8_t pad;   // = 0;
        // len start
        uint64_t time;  // XXX: byte order!
        char name[10];  // first 9 chars of name
        // len end
        uint8_t etx;   // = ETX;  // CRC end
        uint8_t pad2;  // = 0;
        uint8_t crc;   // = 0;
        uint8_t eot;   // = EOT;

    } T_PING;

  public:
    MuSerial(String name, HardwareSerial *pSerial, unsigned long baudRate = 115200,
             uint8_t connectionLed = -1, String inDomainToken = "$remoteName",
             String outDomainToken = "$name")
        : name(name), pSerial(pSerial), baudRate(baudRate), connectionLed(connectionLed),
          inDomainToken(inDomainToken), outDomainToken(outDomainToken) {
        /*! Instantiate a serial link between two muwerk instances.
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
        tID = pSched->add(ft, "serlink", 50000L);  // check every 5ms
        auto fnall = [=](String topic, String msg, String originator) {
            this->subsMsg(topic, msg, originator);
        };
        pSched->subscribe(tID, "#", fnall);
        bCheckLink = true;
        linkState = SYNC;
        if (connectionLed != -1) {
            pinMode(connectionLed, OUTPUT);
            digitalWrite(connectionLed, HIGH);
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

    unsigned char crc(const unsigned char *buf, unsigned int len, unsigned char init = 0) {
        unsigned char c = init;
        for (unsigned int i = 0; i < len; i++)
            c = c ^ buf[i];
        return c;
    }

    void ping() {
        T_PING p = {};
        if (connectionLed != -1)
            digitalWrite(connectionLed, LOW);
        p.soh = SOH;
        p.ver = VER;
        p.num = blockNum++;
        p.cmd = LinkCmd::MUPING;
        p.hLen = 0;
        p.lLen = sizeof(p.time) + 10;
        p.stx = STX;
#ifdef __ARDUINO__
        p.time = (uint64_t)pSched->getUptime();
#else
        p.time = (uint64_t)time(nullptr);
#endif
        strncpy(p.name, name.c_str(), 9);
        p.name[9] = 0;
        p.etx = ETX;
        p.crc = crc((unsigned char *)&(p.ver), sizeof(T_PING) - 3);
        p.eot = EOT;
        pSerial->write((unsigned char *)&p, sizeof(p));
        lastPingSent = pSched->getUptime();
        if (connectionLed != -1)
            digitalWrite(connectionLed, HIGH);
    }

    void sendOut(String topic, String msg) {
        T_HEADER th = {};
        T_FOOTER tf = {};
        unsigned char ccrc;
        unsigned char nul = 0x0;

        // Serial.println("Sending " + topic + ", " + msg);
        th.soh = SOH;
        th.ver = VER;
        th.num = blockNum++;
        th.cmd = LinkCmd::MQTT;
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
        /*
        String inT;
        if (inDomainToken == "$remoteName") {
            inT = remoteName;
        } else {
            inT = inDomainToken;
        }
        if (inT != "") {
            topic = inT + "/" + topic;
        }
        // Serial.println("MuPub: " + topic + ", " + msg + ", from: " + remoteName);
        */

        Serial.println("In: " + topic);
        String pre = name + "/";
        if (topic.substring(0, pre.length()) == pre) {
            topic = topic.substring(pre.length());
        } else {
            pre = inDomainToken + "/";
            if (topic.substring(0, pre.length()) == pre) {
                topic = topic.substring(pre.length());
            }
        }
        topic = remoteName + "/" + topic;
        Serial.println("InPub: " + topic);
        pSched->publish(topic, msg, remoteName);
        return true;
    }

    bool ld = false;
    void loop() {
        unsigned char ccrc;
        unsigned char c;
        if (bCheckLink) {
            if (pSched->getUptime() - lastPingSent > pingPeriod) {
                if (ld) {
                    ld = false;
                    digitalWrite(LED_BUILTIN, LOW);
                } else {
                    ld = true;
                    digitalWrite(LED_BUILTIN, HIGH);
                }
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
                                switch ((LinkCmd)hd.cmd) {
                                case MUPING:
                                    // Serial.println("Ping received");
                                    if (strlen((const char *)&msgBuf[8]) < 10) {
                                        remoteName = (const char *)&msgBuf[8];
                                        lastMsg = pSched->getUptime();
                                        if (!linkConnected) {
                                            linkConnected = true;
                                            pSched->publish(name + "/link/" + remoteName,
                                                            "connected", name);
                                        }
                                    } else {
                                        if (allocated && msgBuf != nullptr) {
                                            free(msgBuf);
                                            msgBuf = nullptr;
                                            allocated = false;
                                        }
                                        linkState = SYNC;
                                        continue;
                                    }
                                    break;
                                case MQTT:
                                    // Serial.println("Msg received");
                                    if (strlen((const char *)msgBuf) + 2 <= msgLen) {
                                        const char *pM =
                                            (const char *)&msgBuf[strlen((const char *)msgBuf) + 1];
                                        if (strlen(pM) + strlen((const char *)msgBuf) + 2 <=
                                            msgLen) {
                                            // Serial.println(
                                            //    "Msg pub: " + String((const char *)msgBuf) + ", "
                                            //    + String(pM));
                                            internalPub((const char *)msgBuf, pM);
                                            lastMsg = pSched->getUptime();
                                        }
                                    }
                                    break;
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
        //       if (originator == name || (remoteName != "" && originator == remoteName)) {
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
