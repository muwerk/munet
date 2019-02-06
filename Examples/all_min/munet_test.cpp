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
    net.begin(&sched);   // connect to WLAN and sync NTP time, credentials read
                         // from SPIFFS, (s.b.)
    mqtt.begin(&sched);  // connect to MQTT server
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
