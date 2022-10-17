munet
=====

[![ESP12e build](https://travis-ci.org/muwerk/munet.svg?branch=master)](https://travis-ci.org/muwerk/munet)
[![Dev Docs](https://img.shields.io/badge/docs-dev-blue.svg)](https://muwerk.github.io/munet/docs/index.html)
[![PlatformIO CI](https://github.com/muwerk/munet/workflows/PlatformIO%20CI/badge.svg)](https://github.com/muwerk/munet/actions)

The munet libraries use the [muwerk scheduler](https://github.com/muwerk/muwerk) to provide a
comprehensive set of network functionality: WiFi connection, Access Point Mode, NTP time sync,
OTA software update and MQTT communication for ESP8266 and ESP32 chips with a minimum of code:

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
    net.begin(&sched);  // connect to WiFi and sync NTP time, credentials read from ESP file system, (s.b.)
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

- WiFi station, access point or both using configuration data from LittleFS/SPIFFS file system (s.b.).
  Connection to the WiFi network is established automatically. The library handles reconnect and error
  recovery gracefully.
- Over-the-air (OTA) update is supported with one line of code [optional]
- Time synchronization with NTP servers, including daylight saving handling [optional]
- Connection to an MQTT server (via PubSubClient) [optional] This transparently connects the pub/sub
  inter-task communication that is provided by the muwerk scheduler with extern MQTT publishers and
  subscribers. Messages between muwerk tasks are published to the external MQTT server, and muwerk
  tasks can transparently subscribe to both other tasks on the same ESP and external topics via the
  MQTT interface.
- MCUs without network hardware can be connected to a second system with network hardware via a
  serial link (`MuSerial.h`). The two connected systems appear as one system to the outside world,
  both can publish/subscribe. pub/sub message synchronization is handled automatically via serial link.

Dependencies
------------

munet relies only on:

- [ustd](https://github.com/muwerk/ustd). Check documentation for required
  [platform defines](https://github.com/muwerk/ustd/blob/master/README.md).
- [muwerk](https://github.com/muwerk/ustd)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON).
  **Note**: Earlier versions used a different lib: ArduinoJson.

| munet component | depends on ustd | muwerk | Arduino_JSON | PubSubClient | Network required | Platforms
| --------------- | --------------- | ------ | ------------ | ------------ | ---------------- | ---------
| Net.h           | x               | x      | x            |              | x                | ESP8266, ESP32
| Ota.h           | x               | x      | x            |              | x                | ESP8266, ESP32
| Mqtt.h          | x               | x      | x            | x            | x                | ESP8266, ESP32
| MuSerial.h      | x               | x      |              |              | serial link between two muwerk nodes | All (only serial port required)

Breaking Changes at Version 0.3.0
---------------------------------

All network classes in Version 3.0.0 have been deeply refactored in order to provide more
functionality and better reliability. Nevertheless there are some breaking changes.

### Changed Method Signatures

`ustd::Net` has now two different `Net::begin` methods. The default one (selected when invoking
`net.begin(&sched);`) starts the network based on configuration stored in a configuration file
located at the filesystem of the device. If the device implements means for managing the
configuration, this is the most versatile method.
There is still a `Net::begin` method with a function signature similar to the older version
that is intended for hardcoded devices. When starting the network with the other `Net::begin`
method, the configuration is stored in memory and connot be changed any more at runtime.

Since the other network classes have also own configuration files when needed, the support for
querying information about network services by publishing `net/services/get` has been removed.

The format of the `net/network/state` message has been extended to cover the new features, but
is still compatible with older versions.

Also the interface of `Mqtt::begin` has changed in order to resemble the fact that all passed
options are the defaults for options **not set** in the configuration file `mqtt.json`.

### Changed Configuration File Format

When the device starts for the first time with the new `munet` library, it will detect an old
configuration file and will migrate the content to the new format automatically. After the
conversion, the filesystem will contain two configuration files:
* `net.json` - the migrated version of the original net.json
* `mqtt.json` - the new configuration file for MQTT if the mqtt service was defined in the old
                configuration file.

See the documentation below for details about the supported configuration options in each file.

### Retained Messages

All messages published by the older version of `ustd::Mqtt` were flagged with the `RETAINED` flag.
This default behaviour has been changed: It is now possible to configure if the default behaviour
should be to flag the messages or not. The default is `false` but if the configuration file was
created by migrating an older `net.json`, the old behaviour will be preserved in order to maintain
compatibility of the device (See configuration option `alwaysRetain` in `mqtt.json`).

When this option is `false` (the default setting), there is a new way (for explicit MQTT topics)
to specify that a message shall be published as `RETAINED`: instead of prefixing the topic with one
exclamation point, the topic shall be prefixed with double exclamation points. E.g.:
`!!homeassistant/config`.

Configuration
-------------

All muwerk network and mqtt configuration is stored in `json` formatted files in the LittleFS/SPIFFS
file system of the ESP chip. In order to initialize a filesystem on a specific device, create a
directory in your local file system of your project named `data` and place all initial configuration
files like `net.json` or `mqtt.json` there.

Using platformio, the initial file system containing all files in your `data` directory is saved to
the ESP chip by executing the following commands:

```bash
pio run -t buildfs
pio run -t updatefs
```

'''Note:''' This project has moved from SPIFFS (deprecated) to LittleFS for ESP8266. To
continue to use SPIFFS on ESP8266 systems, define `USTD_OPTION_FS_FORCE_SPIFFS`. See [USTD](https://github.com/muwerk/ustd/blob/master/README.md#platform-defines) for
more information about configuration options and platform defines.

In order to be able to create LittleFS file system images with platformio,
your `platformio.ini` currently needs to contain:

```
board_build.filesystem = littlefs
```

SPIFFS and LittleFS are not compatible, if the library is updated, a new file system needs to be
created and upload with `pio run -t buildfs` and `pio run -t uploadfs`.

Since ESP32 currently does not (yet) support LittleFS, ESP32 projects automatically use SPIFFS
for the time being.

Minimal Required Configuration Files
------------------------------------

The following minimum configuration files are needed in order to connect to a WiFi network and
have mqtt up and running.

### Minimum `net.json`

```json
{
    "version": 1,
    "mode": "station",
    "hostname": "my-host-name",
    "station": {
        "SSID": "my-network-SSID",
        "password": "myS3cr3t"
    },
    "services": {
        "ntp": {
            "host": ["time.nist.gov"],
            "dstrules": "CET-1CEST,M3.5.0,M10.5.0/3"
        }
    }
}
```

### Minimum `mqtt.json`

```json
{
    "host":"my-mqtt-hostname"
}
```

Network Configuration
---------------------

The network configuration is stored in a file named `net.json`.

### Sample `net.json`

```json
{
    "version": 1,
    "mode": "station",
    "hostname": "muwerk-${macls}",
    "station": {
        "SSID": "my-network-SSID",
        "password": "myS3cr3t",
        "address": "",
        "netmask": "",
        "gateway": "",
        "maxRetries": 40,
        "connectTimeout": 15,
        "rebootonFailure": true
    },
    "ap": {
        "SSID": "muwerk-${macls}",
        "password": "",
        "address": "",
        "netmask": "",
        "gateway": "",
        "channel": 1,
        "hidden": false
    },
    "services": {
        "dns": {
            "host": []
        },
        "ntp": {
            "host": [
                "time.nist.gov",
                "ptbtime1.ptb.de",
                "ptbtime2.ptb.de",
                "ptbtime3.ptb.de"
            ],
            "dstrules": "CET-1CEST,M3.5.0,M10.5.0/3"
        }
    }
}
```

### Configuration Options Placeholder

Some of the configuration options support the use of placeholders in order to allow values that are specific to
a certain devince without the need to create separate configuration files. Placeholders are written in the form
of `${PLACEHOLDER}`.

The following options allow the the of placeholders:
* The `hostname` of the device. The default value of this hostname also uses a plceholder: `muwerk-${macls}`
* The `SSID` for access point mode. Also here the default value uses a plceholder: `muwerk-${macls}`

The following placeholders are currently available:
* `mac`: full mac address
* `macls`: last 4 digits of mac address
* `macfs`: first 4 digits of mac address


### Top Level Configuration Options

| Field        | Usage                                                                                                        |
| ------------ | ------------------------------------------------------------------------------------------------------------ |
| `version`    | The configuration format version number. Current version is `1`. This field is mandatory.                    |
| `deviceID`   | Unique device ID - will be automatically generated and saved on first start. Useful when replacing a device  |
| `mode`       | Operating mode. Can be: `off`, `ap`, `station` or `both`. Default is `ap`                                    |
| `hostname`   | Hostname the device will use and report to other services. May also be used to querythe DHCP server          |
| `ap`         | Configuration options for access point mode. See description below.                                          |
| `station`    | Configuration options for network station mode. See description below.                                       |
| `services`   | Configuration options for network services. See description below.                                           |


#### Configuration Options for Access Point Mode

The following options are stored in the `ap` object and apply to access point mode and dual mdoe.

| Field        | Usage                                                                                          |
| ------------ | ---------------------------------------------------------------------------------------------- |
| `SSID`       | Network name of the wireless network the ESP will host                                         |
| `password`   | Wireless network password                                                                      |
| `address`    | Static IP address. If not defined, the default of the library is taken - usually `192.168.4.1` |
| `netmask`    | Netmask of static IP address. Must be defined if `address` is also defined.                    |
| `gateway`    | Default gateway. Does not really make sense in AP mode, but must be specified.                 |
| `channel`    | Channel used for AP mode. If not specified, channel 1 is used.                                 |
| `hidden`     | If `true`, the network created by the AP is hidden. Default is `false`                         |


#### Configuration Options for Network Station Mode

The following options are stored in the `station` object and apply to network station mode and dual mdoe.

| Field             | Usage                                                                                           |
| ----------------- | ----------------------------------------------------------------------------------------------- |
| `SSID`            | Network name of the wireless network the ESP will join                                          |
| `password`        | Wireless network password                                                                       |
| `address`         | Static IP address. If not defined, the address is obtained via DHCP.                            |
| `netmask`         | Netmask of static IP address. Must be defined if `address` is also defined.                     |
| `gateway`         | Default gateway. Does not really make sense in AP mode, but must be specified.                  |
| `maxRetries`      | Maximum number of retries before giving up (and rebooting). Default is `40`                     |
| `connectTimeout`  | Connection timeout in seconds. Default is 15 seconds.                                           |
| `rebootonFailure` | If `true` the system reboots after reaching `maxRetries` connection failures. Default is `true` |


#### Configuration Options for Network Service DNS Client

The DNS client is configured with an object named `dns` in the `services` object. The DNS client is only
used in network station or dual mode.

| Field        | Usage                                                                                                       |
| ------------ | ----------------------------------------------------------------------------------------------------------- |
| `host`       | Array of hostnames/ip of DNS servers If empty the provided DHCP value is used                               |

#### Configuration Options for Network Service NTP Client

The NTP client is configured with an object named `ntp` in the `services` object. The NTP client is only
used in network station or dual mode.

| Field        | Usage                                                                                                                   |
| ------------ | ----------------------------------------------------------------------------------------------------------------------- |
| `host`       | Array of hostnames/ip of NTP time servers from which the device synchronizes it's time. If empty the DHCP value is used |
| `dstrules`   | optional timezone and daylight saving rules in [unix format](https://mm.icann.org/pipermail/tz/2016-April/023570.html)  |


Network Message Interface
-------------------------

### Incoming

| Topic                       | Message Body        | Description
| --------------------------- | ------------------- | --------------------------------------------------------------------------------------------
| `net/network/get`           |                     | Returns a network information object in json format in a message with topic `net/network`
| `net/network/control`       | `<commands>`        | Starts, stops or restarts the network (put `start`, `stop` or `restart` in the message body)
| `net/networks/get`          | `<options>`         | Requests a WiFi network scan. The list is returned in a message with topic `net/networks`. The additional options `sync` and/or `hidden` can be sent in the body.


### Outgoing

| Topic             | Message Body          | Description
| ----------------- | --------------------- | --------------------------------------------------------------------------------------------
| `net/network`     | `{...}`               | The current network state as JSON object
| `net/rssi`        | `<rssi value>`        | The current signal value when connected to a WiFi
| `net/connections` | `<connection count>`  | The number of stations connected to the device in access point mode
| `net/networks`    | `{..}`                | The result of a WiFi scan as JSON object


MQTT Configuration
------------------

The MQTT configuration is stored in a file named `mqtt.json`.


### Sample `mqtt.json`

```json
{
    "host": "192.168.107.1",
    "port": 1884,
    "username": "",
    "password": "",
    "clientName": "${hostname}",
    "domainToken": "mu",
    "outDomainToken": "omu",
    "lastWillTopic": "",
    "lastWillMessage": "",
    "alwaysRetain": false,
    "subscriptions": [],
    "outgoingBlackList": [],
    "incomingBlackList": []
}
```

### Configuration Options Placeholder

Some of the configuration options support the use of placeholders in order to allow values that are specific to
a certain devince without the need to create separate configuration files. Placeholders are written in the form
of `${PLACEHOLDER}`.

The following options allow the the of placeholders:
* The `clientName` of the device. The default value uses a plceholder: `${hostname}`
* The `lastWillMessage` of the device.

The following placeholders are currently available:
* `mac`: full mac address
* `macls`: last 4 digits of mac address
* `macfs`: first 4 digits of mac address
* `hostname`: hostname of the device


### Top Level Configuration Options

| Field               | Usage                                                                                                        |
| ------------------- | ------------------------------------------------------------------------------------------------------------ |
| `host`              | Hostname or ip address of the MQTT server. This value is mandatory                                           |
| `port`              | Port number under which the MQTT server is reachable. (default: 1884)                                        |
| `username`          | Username for mqtt server authentication. (default: empty for no authentication)                              |
| `password`          | Password for mqtt server authentication. (default: empty for no authentication)                              |
| `clientName`        | The unique MQTT client name.  (default: `${hostname}`)                                                       |
| `domainToken`       | Common domain token for device group. (default' `mu`)                                                        |
| `outDomainToken`    | Domain token for outgoing messages. (default: `omu`)                                                         |
| `lastWillTopic`     | Topic of MQTT last will message. (default: `<outDomainName>/<clientName>/mqtt/state`)                        |
| `lastWillMessage`   | Message content for last will message. (default: `disconnected`)                                             |
| `alwaysRetain`      | If `true` all messages published to MQQ will flagged as `RETAINED`. (default: `false`)                       |
| `subscriptions`     | List of additional subscription to route into the scheduler's message queue. (default: empty)                |
| `outgoingBlackList` | List of topics and topic wildcards that will not be published to the external server                         |
| `incomingBlackList` | List of topics and topic wildcards that will not be published to the muwerk scheduler's message queue        |


MQTT Message Interface
----------------------

### Incoming

| Topic                       | Message Body       | Description
| --------------------------- | ------------------ | --------------------------------------------------------------------------------------------
| `mqtt/outgoingblock/set`    | `topic[-wildcard]` | A topic or a topic wildcard for topics that should not be forwarded to the external mqtt server (e.g. to prevent message spam or routing problems) |
| `mqtt/outgoingblock/remove` | `topic[-wildcard]` | Remove a block on a given outgoing topic wildcard. |
| `mqtt/incomingblock/set`    | `topic[-wildcard]` | A topic or a topic wildcard for topics that should not be forwarded from the external mqtt server to muwerk. |
| `mqtt/incomingblock/remove` | `topic[-wildcard]` | Remove a block on a given incoming topic wildcard. |

### Outgoing

| Topic         | Message Body                           | Description
| ------------- | -------------------------------------- | --------------------------------------------------------------------------------------------
| `mqtt/config` | `<prefix>+<will_topic>+<will_message>` | The message contains three parts separated bei `+`: prefix, the last-will-topic and last-will message. `prefix` is the mqtt topic-prefix automatically prefixed to outgoing messages, composed of `omu` (set with mqtt) and `hostname`, e.g. `omu/myhost`. `prefix` can be useful for mupplets to know the actual topic names that get published externally.
| `mqtt/state`  | `connected` or `disconnected`          | muwerk processes that subscribe to `mqtt/state` are that way informed, if mqtt external connection is available. The `mqtt/state` topic with message `disconnected` is also the default configuration for mqtt's last will topic and message.

MuSerial - exchange of MQTT pub/sub messages between two muwerk MCUs via serial link
------------------------------------------------------------------------------------

Two muwerk systems can be linked via a serial connection, if both run `MuSerial`. Pub/Sub Messages
are exchanged transparently over the serial link. If one of the systems is connected to an external
MQTT server, then both systems have access to MQTT pub/sub. To the outside, the system appears as
one system, e.g. sensors or actors can be connected to either system, behaving identical to the outside
world.

That way, a muwerk MCU without networking hardware can be connected to a network via a second muwerk
system with network access via MuSerial.

See [Example SerialBridge](https://github.com/muwerk/examples/tree/master/serialBridge) for a complete overview.

History
-------

- 0.4.1 (2022-10-17): Support for ESP32C3 (platform define `__ESP32_RISC__`).
- 0.4.0 (2021-01-30): **Breaking change** for ustd library include: ustd include-files have now `ustd_` prefix to prevent name-clashes with various platform-sdks. [queue.h clashed with ESP8266-Wifi, platform.h clashed with
RISC-V SDK, hence new names `ustd_queue.h` and `ustd_platform.h` etc.]
- 0.3.2 (2021-01-29): MuSerial: MQTT-enable non-networked hardware via serial link, NTP Bugfix, MQTT connection state fix.
  - Bugfix: NTP initialization on ESP8266 [failed often, #6](https://github.com/muwerk/munet/issues/6), due to unsafe parameter handling
   in current configTime() API of ESP8266, fixed.
  - MuSerial: MQTT-via-serial link between two muwerk MCUs to provide MQTT access to hardware without network via serial.
    MuSerial runs on all platforms and can be used to network-enable hardware via ESPs. See [SerialBridge](https://github.com/muwerk/examples/tree/master/serialBridge).
  - Fixed handling of mqtt connection state when network connection changes
- CI (2021-01-38): Github actions build-check for all supported platforms: network for ESPxx, serial for all others.
- 0.3.1 (2021-01-20): Minor change: enable dependency-managment for Arduino library manager.
- 0.3.0 (2021-01-20): Next Generation Network: See section _"Breaking Changes at Version 0.3.0"_ for caveats.
  - Support for Access Point mode and Dual Mode (both network station and access point mode)
  - Support for enhanced network scans (async and display of hidden networks)
  - Interface for controlling network operations (start, stop, restart)
  - Detailed and extensible configuration for network and MQTT
  - Support for controlling the `RETAINED` flag in published messages
- 0.2.1 (2021-01-02): Small breaking change: the format of the `mqtt/state` has been simplified: the message contains either `connected` or `disconnected`. Configuration information has been moved into a separate message `mqtt/config`. Support for no outgoing domain prefix (no 'omu') fixed.
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

- In order to build MQTT for ESP32, PubSubClient v2.7 is needed (PubSubClient 2.8 crashes!).
- SPIFFS filesystem: Optionally use this [Arduino plugin](https://github.com/me-no-dev/arduino-esp32fs-plugin) to upload the SPIFFS filesystem to ESP32.

## References

- [ustd](https://github.com/muwerk/ustd) microWerk standard library
- [muWerk](https://github.com/muwerk/muwerk) microWerk scheduler
- [mupplets](https://github.com/muwerk/mupplets) sensor and io functionality blocks
- [PubSubClient](https://github.com/knolleary/pubsubclient) the excellent MQTT library used by munet.
- [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON) JSON library for Arduino
- Time zone rules: https://mm.icann.org/pipermail/tz/2016-April/023570.html
