// ota.h
#pragma once

// #if defined(__ESP__)

#include <functional>

#include "ustd_platform.h"
#include "ustd_array.h"
#include "ustd_map.h"

#include "scheduler.h"
#include "filesystem.h"

#include <ArduinoOTA.h>
#include <Arduino_JSON.h>

namespace ustd {

/*! \brief munet OTA Class

The OTA object listens for network connections and automatically establishes OTA update funcionality
on successful connection to WiFi. In case of software update, all other muwerk tasks are
automatically halted, and software update is granted priority.

Network failures are handled automatically.

## Sample OTA Integration

\code{cpp}
#define __ESP__ 1   // Platform defines required, see doc, mainpage.
#include "scheduler.h"
#include "net.h"
#include "ota.h"

ustd::Scheduler sched;
ustd::Net net();
ustd::Ota ota;

void setup() {
    net.begin(&sched);
    ota.begin(&sched);
}
\endcode

Security note: currently the API doesn't support setting an OTA password. Please use:

\code{cpp}
ArduinoOTA.setPassword("secret");
// or:
// MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
\endcode

to set an OTA password. This will be supported within the API in a future version.
 */
class Ota {
  private:
    // muwerk task management
    Scheduler *pSched;
    int tID;

    // runtime control - state management
    bool bNetUp = false;
    bool bCheckOTA = false;
    bool bOTAUpdateActive = false;

  public:
    Ota() {
        //! Instantiate a over-the-air (OTA) software update object.
    }

    ~Ota() {
    }

    void begin(Scheduler *_pSched) {
        /*! Setup OTA over-the-air software update.
         *
         * This activates the OTA interface. As soon as a network connection is available, listening
         * for OTA requests are started. Handling of network connections and disconnnects is done
         * automatically and does not require further interaction.
         *
         * @param _pSched Pointer to the muwerk scheduler.
         */
        // init scheduler
        pSched = _pSched;
        tID = pSched->add([this]() { this->loop(); }, "ota", 25000L);  // check for ota every 25ms

        // subscribe to all messages
        pSched->subscribe(tID, "#", [this](String topic, String msg, String originator) {
            this->subsMsg(topic, msg, originator);
        });

        pSched->publish("net/network/get");
    }

  private:
    void loop() {
        if (bCheckOTA) {
            ArduinoOTA.handle();
        }
    }

    void subsMsg(String topic, String msg, String originator) {
        JSONVar mqttJsonMsg = JSON.parse(msg);

        if (JSON.typeof(mqttJsonMsg) == "undefined") {
            return;
        }

        if (topic == "net/network") {
            String state = (const char *)mqttJsonMsg["state"];  // root["state"];
            if (state == "connected") {
                if (!bNetUp) {
                    bNetUp = true;
                    OTAsetup();
                    bCheckOTA = true;
                }
            } else {
                bNetUp = false;
                bCheckOTA = false;
            }
        }
    }

    void OTAsetup() {

#if defined(__ESP32__)
        ArduinoOTA.setHostname(WiFi.getHostname());
#else
        ArduinoOTA.setHostname(WiFi.hostname().c_str());
#endif

        // TODO: No authentication by default
        // ArduinoOTA.setPassword("secret");

        // Password can be set with it's md5 value as well
        // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
        // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

        ArduinoOTA.onStart([&]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
                type = "sketch";
            else  // U_SPIFFS
                type = "filesystem";

            DBG("Start updating " + type);
            bOTAUpdateActive = true;
            pSched->singleTaskMode(tID);
            fsEnd();
        });
        ArduinoOTA.onEnd([&]() {
            DBG("\nEnd of update");
            pSched->singleTaskMode(-1);
            bOTAUpdateActive = false;
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            DBGF("Progress: %u%%\r", (progress / (total / 100)));
        });
        ArduinoOTA.onError([&](ota_error_t error) {
#ifdef USE_SERIAL_DBG
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR)
                Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR)
                Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR)
                Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR)
                Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR)
                Serial.println("End Failed");
#endif
        });
        ArduinoOTA.begin();
    }
};

}  // namespace ustd

// #endif  // defined(__ESP__)
