# munet
Network WLAN access, OTA, NTP time and MQTT (via PubSubClient) for ESP8266 and ESP32

## Configuration

The network configuration is stored in a `json` file `data/net.json`

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
* Time zone rules: https://mm.icann.org/pipermail/tz/2016-April/023570.html
