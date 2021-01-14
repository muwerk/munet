# munet

[![ESP12e build](https://travis-ci.org/muwerk/munet.svg?branch=master)](https://travis-ci.org/muwerk/munet)
[![Dev Docs](https://img.shields.io/badge/docs-dev-blue.svg)](https://muwerk.github.io/munet/docs/index.html)

The munet libraries use the [muwerk scheduler](https://github.com/muwerk/muwerk) to provide a comprehensive set of network functionality: WLAN connection, NTP time sync, OTA software update and MQTT communication for ESP8266 and ESP32 chips with a minimum of code:

```c++
#define __ESP__   // Platform define, add #define __ESP32__ for ESP32 (see dependencies)
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
    net.begin(&sched);  // connect to WLAN and sync NTP time, credentials read from ESP file system, (s.b.)
    mqtt.begin(&sched); // connect to MQTT server
    ota.begin(&sched);  // enable OTA updates

    int tID = sched.add(appLoop, "main");  // create task for your app code
}

void appLoop() {
    // your code goes here.
}

// Never add code to this loop, use appLoop() instead.
void loop() {
    sched.loop();
}

```

The library provides:

- Network WLAN access, using credentials read from LittleFS/SPIFFS file system (s.b.), automatic connection to a WLAN is established. The library handles re-connect and error recovery gracefully.
- Over-the-air (OTA) update is supported with one line of code [optional]
- Time synchronization with NTP servers, including daylight saving handling [optional]
- Connection to an MQTT server (via PubSubClient) [optional] This transparently connects the pub/sub inter-task communication that is provided by the muwerk scheduler with extern MQTT publishers and subscribers. Messages between muwerk tasks are published to the external MQTT server, and muwerk tasks can transparently subscribe to both other tasks on the same ESP and external topics via the MQTT interface.

## Dependencies

Munet relies only on:

- [ustd](https://github.com/muwerk/ustd). Check documentation for required [platform defines](https://github.com/muwerk/ustd/blob/master/README.md).
- [muwerk](https://github.com/muwerk/ustd)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON). **Note**: Earlier versions used a different lib: ArduinoJson.

| munet component | depends on ustd | muwerk | Arduino_JSON | PubSubClient |
| --------------- | --------------- | ------ | ------------ | ------------ |
| Net.h           | x               | x      | x            |              |
| Ota.h           | x               | x      | x            |              |
| Mqtt.h          | x               | x      | x            | x            |

## Configuration

The network configuration is stored in a `json` formatted file `net.json` in the LittleFS/SPIFFS file system of the ESP chip. Create a copy in your local file system of your project at `data/net.json`.

'''Note:''' This project is currently preparing to move from SPIFFS (deprecated) to LittleFS. To continue
to use SPIFFS, define `__USE_OLD_FS__`. In order to activate LittleFS, your `platformio.ini` currently
needs to contain:

```
board_build.filesystem = littlefs
```

SPIFFS and LittleFS are not compatible, if the library is updated, a new file system needs to be created and upload with `pio run -t buildfs` and `pio run -t uploadfs`.

Since ESP32 currently does not (yet) support LittleFS, ESP32 projects require the define `__USE_OLD_FS__` to continue to use SPIFFS for the time being.

### Sample `net.json`

```json
{
  "SSID": "my-network-SSID",
  "password": "myS3cr3t",
  "hostname": "myhost",
  "services": [
    { "timeserver": "time.nist.gov" },
    { "dstrules": "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "mqttserver": "my.mqtt.server" }
  ]
}
```

| Field      | Usage                                                                                                                  |
| ---------- | ---------------------------------------------------------------------------------------------------------------------- |
| SSID       | Network name of the wireless network the ESP will join                                                                 |
| password   | Wireless network password                                                                                              |
| hostname   | Hostname the ESP will try to register at the DHCP server                                                               |
| timeserver | optional address of an NTP time server, if given, ESP time will be synchronized                                        |
| dstrules   | optional timezone and daylight saving rules in [unix format](https://mm.icann.org/pipermail/tz/2016-April/023570.html) |
| mqttserver | optional address of MQTT server ESP connects to                                                                        |

Using platformio, `data/net.json` is saved to the ESP chip using:

```bash
pio run -t buildfs
pio run -t updatefs
```

#### Internal Messages sent by mqtt to other muwerk processes:

| topic        | message body   | comment                                |
| ------------ | -------------- | -------------------------------------- |
| `mqtt/config` | `<prefix>+<will_topic>+<will_message>` | The message contains three parts separated bei `+`: prefix, the last-will-topic and last-will message. `prefix` is the mqtt topic-prefix automatically prefixed to outgoing messages, composed of `omu` (set with mqtt) and `hostname`, e.g. `omu/myhost`. `prefix` can be useful for mupplets to know the actual topic names that get published externally. |
| `mqtt/state` | `connected` or `disconnected` | muwerk processes that subscribe to `mqtt/state` are that way informed, if mqtt external connection is available. The `mqtt/state` topic with message `disconnected` is also the default configuration for mqtt's last will topic and message. |

#### Messages received by mqtt:

| topic        | message body   | comment                                |
| ------------ | -------------- | -------------------------------------- |
| `mqtt/outgoingblock/set` | `topic[-wildcard` | A topic or a topic wildcard for topics that should not be forwarded to the external mqtt server (e.g. to prevent message spam or routing problems) |
| `mqtt/outgoingblock/remove` | `topic[-wildcard` | Remove a block on a given outgoing topic wildcard. |
| `mqtt/incomingblock/set` | `topic[-wildcard` | A topic or a topic wildcard for topics that should not be forwarded from the external mqtt server to muwerk. |
| `mqtt/incomingblock/remove` | `topic[-wildcard` | Remove a block on a given incoming topic wildcard. |

## History

- 0.2.1 (2021-01-02) : Small breaking change: the format of the `mqtt/state` has been simplified: the message contains either `connected` or `disconnected`. Configuration information has been moved into a separate message `mqtt/config`. Support for no outgoing domain prefix (no 'omu') fixed.
- 0.2.0 (2020-12-25): Initial support for LittleFS on ESP8266.
- 0.1.99 2020-09 (not yet released): Ongoing preparations for switch to LittleFS, since SPIFFS is deprecated.
- 0.1.11 (2019-12-27): New mqtt.h api functions `addSubscription()`, `removeSubscription()` that
  allow to import additional topics from external MQTT server. See [API doc](https://muwerk.github.io/munet/docs/classustd_1_1Mqtt.html) for details.
- 0.1.10 (2019-11-17): Add information about external prefix to `mqtt/state` messages. (HA registration of mupplets requirement)
- 0.1.9 (2019-11-17): Allow publishing to unmodified topics using `!` topic-prefix. Send internal messages with topic `mqtt/state`
- 0.1.8 (2019-11-03): NTP Init was unreliable, fixed.
- 0.1.7 (2019-11-03): ESP32 crashes, if configTime() [for NTP setup] is called while WLAN is not connected. Fixes #2.
- 0.1.6 (2019-08-06): Outgoing messages (from ESP to MQTT server) are now prefixed by an additional outDomainToken in order
  to prevent recursions. Older versions did only prefix with ESP's hostname, but at the same time the ESP MQTT client also subscribes
  to topics starting with it's hostname. This design error caused duplicated messages.
- 0.1.5 (2019-07-24): This version uses [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON) for JSON parsing. Older versions relied on outdated versions of the older library `ArduinoJson` which is no longer supported with `muwerk`.

## Errata
- MQTT doesn't seem to run stable with latest `PubSubClient` v2.8. It is recommended to use `PubSubClient@2.7` for the time being. This seems to affect
  both ESP8266 and ESP32

## Documentation

- [ustd::munet documentation.](https://muwerk.github.io/munet/docs/index.html)
- [ustd::muwerk documentation.](https://muwerk.github.io/muwerk/docs/index.html)
- `ustd` required [platform defines.](https://github.com/muwerk/ustd/blob/master/README.md)
- [ustd::ustd documentation.](https://muwerk.github.io/ustd/docs/index.html)

## ESP32 notes

- In order to build MQTT for ESP32, PubSubClient v2.7 or newer is needed.
- SPIFFS filesystem: Optionally use this [Arduino plugin](https://github.com/me-no-dev/arduino-esp32fs-plugin) to upload the SPIFFS filesystem to ESP32.

## References

- [ustd](https://github.com/muwerk/ustd) microWerk standard library
- [muWerk](https://github.com/muwerk/muwerk) microWerk scheduler
- [mupplets](https://github.com/muwerk/mupplets) sensor and io functionality blocks
- [PubSubClient](https://github.com/knolleary/pubsubclient) the excellent MQTT library used by munet.
- [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON) JSON library for Arduino
- Time zone rules: https://mm.icann.org/pipermail/tz/2016-April/023570.html
