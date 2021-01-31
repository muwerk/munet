#define __ESP__


// See: https://github.com/muwerk/ustd#platform-defines for other platforms:
// ESP8266:

#include "scheduler.h"
#include "net.h"

ustd::Scheduler sched;
ustd::Net net;


void appLoop();

void setup() {
    // See: https://github.com/muwerk/munet for net.json and mqtt.json configuration
    net.begin(&sched);   // connect to WLAN and sync NTP time, credentials read
                         // from SPIFFS file net.json

    sched.add(appLoop, "main");  // create task for your app code
}

void appLoop() {
    // your code goes here.
}

// Never add code to this loop, use appLoop() instead.
void loop() {
    sched.loop();
}
