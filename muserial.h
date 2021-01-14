// muserial.h
#pragma once

#include "platform.h"
#include "array.h"
#include "map.h"

#include "scheduler.h"
//#include <Arduino_JSON.h>

#ifdef __ARDUINO__
#include <time.h>
#endif

#ifdef __ATTINY__
#include <time.h>
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
    time_t lastRead = 0;
    time_t lastMsg = 0;
    bool linkConnected = false;
    unsigned long readTimeout = 5;          // sec
    unsigned long pingReceiveTimeout = 10;  // sec
    String remoteName = "";
    String inDomainToken;
    String outDomainToken;
    ustd::array<String> outgoingBlockList;
    ustd::array<String> incomingBlockList;

    const uint8_t SOH = 0x01, STX = 0x02, ETX = 0x03, EOT = 0x04;
    const uint8_t VER = 0x01;

    enum LinkCmd { MUPING, MQTT };

    typedef struct t_header {
        unsigned char ver;   // = VER;  // CRC start
        unsigned char num;   // block number
        unsigned char cmd;   // LinkCmd
        unsigned char hLen;  // = 0;  // Hi byte length
        unsigned char lLen;  // = 0;  // Lo byte length
        unsigned char stx;   // = STX;
    } T_HEADER;

    typedef struct t_footer {
        unsigned char etx;  // = ETX;  // CRC end
        unsigned char crc;  // = 0;
        unsigned char eot;  // = EOT;
    } T_FOOTER;

    typedef struct t_ping {
        unsigned char soh;   // = SOH;
        unsigned char ver;   // = VER;  // CRC start
        unsigned char num;   // block number
        unsigned char cmd;   // LinkCmd
        unsigned char hLen;  // = 0;  // Hi byte length
        unsigned char lLen;  // = 0;  // Lo byte length
        unsigned char stx;   // = STX;
        // len start
        unsigned long time;  // XXX: byte order!
        char name[10];       // first 9 chars of name
        // len end
        unsigned char etx;  // = ETX;  // CRC end
        unsigned char crc;  // = 0;
        unsigned char eot;  // = EOT;

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

        /*std::function<void()>*/ auto ft = [=]() { this->loop(); };
        tID = pSched->add(ft, "serlink", 50000L);  // check every 50ms
        /*std::function<void(String, String, String)>*/ auto fnall = [=](String topic, String msg,
                                                                         String originator) {
            this->subsMsg(topic, msg, originator);
        };
        pSched->subscribe(tID, "#", fnall);
        bCheckLink = true;
        linkState = SYNC;
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
        T_PING p;
        memset(&p, 0, sizeof(p));
        p.soh = SOH;
        p.ver = VER;
        p.num = blockNum++;
        p.cmd = LinkCmd::MUPING;
        p.hLen = 0;
        p.lLen = sizeof(unsigned long) + 10;
        p.stx = STX;
        p.time = time(nullptr);
        strcpy(p.name, name.substring(0, 9).c_str());
        p.etx = ETX;
        p.crc = crc((unsigned char *)&(p.ver), sizeof(T_PING) - 3);
        p.eot = EOT;
        pSerial->write((unsigned char *)&p, sizeof(p));
    }

    void sendOut(String topic, String msg) {
        T_HEADER th;
        T_FOOTER tf;
        unsigned char ccrc;
        unsigned char startbyte = SOH;
        unsigned char nul = 0x0;
        memset(&th, 0, sizeof(th));
        memset(&tf, 0, sizeof(tf));

        th.ver = VER;
        th.num = blockNum++;
        th.cmd = LinkCmd::MQTT;
        unsigned int len = topic.length() + msg.length() + 2;
        th.hLen = len / 256;
        th.lLen = len % 256;
        th.stx = STX;

        tf.etx = ETX;
        tf.etx = EOT;

        ccrc = crc((const unsigned char *)&th, 6);
        ccrc = crc((const unsigned char *)topic.c_str(), topic.length(), ccrc);
        ccrc = crc(&nul, 1, ccrc);
        ccrc = crc((const unsigned char *)msg.c_str(), msg.length(), ccrc);
        ccrc = crc(&nul, 1, ccrc);
        ccrc = crc((const unsigned char *)&fo, 1, ccrc);

        fo.crc = ccrc;

        pSerial->write(&startbyte, 1);
        pSerial->write((unsigned char *)&th, 6);
        pSerial->write((unsigned char *)topic.c_str(), topic.length());
        pSerial->write(&nul, 1);
        pSerial->write((unsigned char *)msg.c_str(), msg.length());
        pSerial->write(&nul, 1);
        pSerial->write((unsigned char *)&tf, 3);
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
    LinkState state;

    bool internalPub(String topic, String msg) {
        for (unsigned int i = 0; i < incomingBlockList.length(); i++) {
            if (Scheduler::mqttmatch(topic, incomingBlockList[i]))
                return false;
        }
        String inT;
        if (inDomainToken == "$remoteName") {
            inT = remoteName;
        } else {
            inT = inDomainToken;
        }
        if (inT != "") {
            topic = inT + "/" + topic;
        }
        pSched->publish(topic, msg, remoteName);
        return true;
    }

    void loop() {
        unsigned char ccrc;
        unsigned char c;
        if (bCheckLink) {
            while (pSerial->available() > 0) {
                c = pSerial->read();
                lastRead = time(nullptr);
                switch (state) {
                case SYNC:
                    if (c == SOH) {
                        state = HEADER;
                        pHd = (unsigned char *)&hd;
                        hLen = 0;
                    }
                    break;
                case HEADER:
                    pHd[hLen] = c;
                    hLen++;
                    if (hLen == 6) {
                        // XXX: check block number
                        if (hd.ver != VER || hd.stx != STX) {
                            state = SYNC;
                        } else {
                            msgLen = 256 * hd.hLen + hd.lLen;
                            msgBuf = (unsigned char *)malloc(msgLen);
                            curMsg = 0;
                            if (msgBuf) {
                                state = MSG;
                                allocated = true;
                            } else {
                                state = SYNC;
                            }
                        }
                    }
                    break;
                case MSG:
                    msgBuf[curMsg] = c;
                    ++curMsg;
                    if (curMsg == msgLen) {
                        state = CRC;
                        cLen = 0;
                    }
                    break;
                case CRC:
                    pFo[cLen] = c;
                    ++cLen;
                    if (cLen == 3) {
                        if (fo.etx != ETX || fo.eot != EOT) {
                            if (allocated && msgBuf != nullptr) {
                                free(msgBuf);
                                allocated = false;
                            }
                            state = SYNC;
                            continue;
                        }
                    }
                    ccrc = crc((const unsigned char *)&hd, 6);
                    ccrc = crc(msgBuf, msgLen, ccrc);
                    ccrc = crc((const unsigned char *)&fo, 1, ccrc);
                    if (ccrc != fo.crc) {
                        if (allocated && msgBuf != nullptr) {
                            free(msgBuf);
                            allocated = 0;
                        }
                        state = SYNC;
                        continue;
                    } else {
                        switch ((LinkCmd)hd.cmd) {
                        case MUPING:
                            if (strlen((const char *)&msgBuf[4]) < 10) {
                                remoteName = (const char *)&msgBuf[4];
                                lastMsg = time(nullptr);
                                if (!linkConnected) {
                                    linkConnected = true;
                                    pSched->publish(name + "/link", "connected");
                                }
                            } else {
                                if (allocated && msgBuf != nullptr) {
                                    free(msgBuf);
                                    allocated = 0;
                                }
                                state = SYNC;
                                pSched->publish(name + "/ping", remoteName);
                                continue;
                            }
                            break;
                        case MQTT:
                            if (strlen((const char *)msgBuf) + 2 < msgLen) {
                                const char *pM =
                                    (const char *)&msgBuf[strlen((const char *)msgBuf) + 1];
                                if (strlen(pM) + strlen((const char *)msgBuf) + 2 < msgLen) {
                                    internalPub((const char *)msgBuf, pM);
                                    lastMsg = time(nullptr);
                                }
                            }
                            break;
                        }
                        if (allocated && msgBuf != nullptr) {
                            free(msgBuf);
                            allocated = 0;
                        }
                        state = SYNC;
                    }
                    break;
                }
            }
        } else {
            if (state != SYNC) {
                if ((unsigned long)(time(nullptr) - lastRead) > readTimeout) {
                    state = SYNC;
                    if (linkConnected) {
                        pSched->publish(name + "/link", "disconnected");
                    }
                    linkConnected = false;
                    if (allocated) {
                        free(msgBuf);
                        msgBuf = nullptr;
                    }
                }
            } else {
                if ((unsigned long)(time(nullptr) - lastMsg) > pingReceiveTimeout) {
                    if (linkConnected) {
                        pSched->publish(name + "/link", "disconnected");
                    }
                    linkConnected = false;
                }
            }
        }
    }

    void subsMsg(String topic, String msg, String originator) {
        if (originator == name) {
            // prevent loops;
            return;
        }
        for (unsigned int i = 0; i < outgoingBlockList.length(); i++) {
            if (Scheduler::mqttmatch(topic, outgoingBlockList[i])) {
                // blocked.
                return;
            }
        }
        sendOut(topic, msg);
    };
};

}  // namespace ustd
