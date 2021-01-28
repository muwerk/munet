// This example requires the arduino libraries:
// - ustd
// - muwerk
// Platform with network (ESPx):
// - Arduino_JSON
// - PubSubClient

#include "scheduler.h"

#if defined(USTD_FEATURE_NET)
#include "net.h"
#include "mqtt.h"
#include "ota.h"
#endif

#include "muserial.h"

ustd::Scheduler sched;

#if defined(USTD_FEATURE_NET)
ustd::Net net(LED_BUILTIN);
ustd::Mqtt mqtt;
ustd::Ota ota;
#endif

#if defined(QUIRK_RENAME_SERIAL)
// Feather M0 fix for non-standard port-name
#pragma message("Quirk active")
#define Serial Serial5
#endif

ustd::MuSerial serlink("serlink", &Serial, 115200);

void appLoop();

void setup() {
#if defined(USTD_FEATURE_NET)
    // See: https://github.com/muwerk/munet for net.json and mqtt.json configuration
    net.begin(&sched);   // connect to WLAN and sync NTP time, credentials read
                         // from SPIFFS file net.json
    mqtt.begin(&sched);  // connect to MQTT server (address from mqtt.json)
    ota.begin(&sched);   // enable OTA updates
#endif
    serlink.begin(&sched);  // enable serial link to another muwerk platform
                            // to exchange pub/sub messages via serial

    sched.add(appLoop, "main");  // create task for your app code
}

void appLoop() {
    // your code goes here.
}

// Never add code to this loop, use appLoop() instead.
void loop() {
    sched.loop();
}
