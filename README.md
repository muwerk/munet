# munet

The munet libraries use the [muwerk scheduler](https://github.com/muwerk/muwerk) to provide a comprehensive set of network functionality: WLAN connection, NTP time sync, OTA software update and MQTT communication for ESP8266 and ESP32 chips with a minimum of code:

```c++
#include "scheduler.h"
#include "net.h"
#include "mqtt.h"
#include "ota.h"

ustd::Scheduler sched;
ustd::Net net(LED_BUILTIN);
ustd::Mqtt mqtt;
ustd::Ota ota;

void setup() {
    net.begin(&sched);  // connect to WLAN and sync NTP time
    mqtt.begin(&sched); // connect to MQTT server
    ota.begin(&sched);  // enable OTA updates
    
    tID = sched.add(appLoop, "main");  // create task for your app code
}

void appLoop() {
    // your code goes here.
}

// Never add code to this loop, use appLoop() instead.
void loop() {
    sched.loop();
}

```

Requirements: packages `ustd`, `muwerk`, `munet`

The library provides:

* Network WLAN access, using credentials read from SPIFFS file system (s.b.), automatic connection to a WLAN is established. The library handles re-connect and error recovery gracefully.
* Over-the-air (OTA) update is supported with one line of code [optional]
* Time synchronization with NTP servers, including daylight saving handling [optional]
* Connection to an MQTT server (via PubSubClient) [optional]

## Configuration

The network configuration is stored in a `json` formatted file `net.json` in the SPIFFS file system of the ESP chip. Create a copy in your local file system of your project at `data/net.json`.

### Sample `net.json`

```json
{
"SSID":"my-network-SSID",
"password":"myS3cr3t",
"hostname":"myhost",
"services": [
   {"timeserver": "time.nist.gov"},
   {"dstrules": "CET-1CEST,M3.5.0,M10.5.0/3"},
   {"mqttserver": "my.mqtt.server"}
]
}
```

| Field         | Usage                                                                           |
| ------------- | ------------------------------------------------------------------------------- |
| SSID          | Network name of the wireless network the ESP will join                          |
| password      | Wireless network password                                                       |
| hostname      | Hostname the ESP will try to register at the DHCP server                        |
| timeserver    | optional address of an NTP time server, if given, ESP time will be synchronized |
| dstrules      | optional timezone and daylight saving rules in [unix format](https://mm.icann.org/pipermail/tz/2016-April/023570.html)                      |
| mqttserver    | optional address of MQTT server ESP connects to                                 |

Using platformio, `data/net.conf` is saved to the ESP chip using:

```bash
pio run -t buildfs
pio run -t updatefs
```



## ESP32 notes

* In order to build MQTT for ESP32, this patch needs to be applied: https://github.com/knolleary/pubsubclient/pull/336
* SPIFFS filesystem: Optionally use this [Arduino plugin](https://github.com/me-no-dev/arduino-esp32fs-plugin) to upload the SPIFFS filesystem to ESP32.

## References
* [ustd](https://github.com/muwerk/ustd) microWerk standard library
* [muWerk](https://github.com/muwerk/muwerk) microWerk scheduler
* [mupplets](https://github.com/muwerk/mupplets) sensor and io functionality blocks
* [pubsubclient](https://github.com/knolleary/pubsubclient) the excellent MQTT library used by munet.
* Time zone rules: https://mm.icann.org/pipermail/tz/2016-April/023570.html
