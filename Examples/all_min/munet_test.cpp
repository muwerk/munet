// This example requires the arduino libraries:
// - ustd
// - muwerk
// - Arduino_JSON
// - PubSubClient

#define __ESP__  // Platform define, add #define __ESP32__ for ESP32 (see
                 // dependencies)
#include "scheduler.h"
#include "net.h"
#include "mqtt.h"
#include "ota.h"

ustd::Scheduler sched;
ustd::Net net(LED_BUILTIN);
ustd::Mqtt mqtt;
ustd::Ota ota;

void appLoop();

void setup() {
    // See: https://github.com/muwerk/munet for net.json configuration
    net.begin(&sched);   // connect to WLAN and sync NTP time, credentials read
                         // from SPIFFS file net.json
    mqtt.begin(&sched);  // connect to MQTT server (address from mqtt.json)
    ota.begin(&sched);   // enable OTA updates

    int tID = sched.add(appLoop, "main");  // create task for your app code
}

void appLoop() {
    // your code goes here.
}

// Never add code to this loop, use appLoop() instead.
void loop() {
    sched.loop();
}
