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
    String remoteName;

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
             uint8_t connectionLed = -1)
        : name(name), pSerial(pSerial), baudRate(baudRate), connectionLed(connectionLed) {
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
                                    pSched->publish((const char *)msgBuf, pM, remoteName);
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

    void subsMsg(String topic, String msg, String originator){};
};

}  // namespace ustd
