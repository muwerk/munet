Prepare your Arduino-IDE for usage with ESP and muwerk and munet
================================================================

Board support for ESP8266 and/or ESP32 installation
---------------------------------------------------

If you want to use ESP8266 or ESP32 based boards, open `Preferences` in Arduino IDE,
click on the Windows-Icon right to `Additional Boards Manager URLs` and
add new entries (each in a new line):

For ESP8266: `https://arduino.esp8266.com/stable/package_esp8266com_index.json`

For ESP32: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

For Adafruit feather boards: `https://www.adafruit.com/package_adafruit_index.json`

You find the original descriptions here [for ESP8266](https://github.com/esp8266/Arduino#installing-with-boards-manager) and [for ESP32](https://github.com/espressif/arduino-esp32/blob/master/docs/arduino-ide/boards_manager.md)

![Arduino IDE Preferences](https://github.com/muwerk/munet/blob/master/extras/arduino-ide.jpg?raw=true)

Once preferences is closed, go to boards manager and select the board you are using (e.g. ESP8266). This
installs the tools and compilers needed, and afterwards you can select your specific board.

Add muwerk libraries
--------------------

To use munet, you need to make sure that the library `munet` and it's dependencies is
installed. Open Arduino IDE's `Library Manager` and search for: `munet`.
The installation process will ask you to additionally install `Muwerk ustd library`
and `Muwerk scheduler library`. Both libraries are needed, so accept this.

If you just want to use `MuSerial` serial connections, you're done with library installations.

For networking, you will additionally need to install: `Arduino_JSON` (not to be confused with
ArduinoJson, which is a different library that does not work with muwerk) and for MQTT you
will need `PubSubClient`: here select version `2.7.`. Make sure **not to install** the latest version `2.8` 
(at the time of this writing), since it does *not* run stable with muwerk.

So for full networking, you should have installed `Muwerk ustd library`, `Muwerk scheduler library`, `munet`,
`Arduino_JSON`, and `PubSubClient` Version 2.7.

Update Libraries
----------------

From time to time, it's good practice to update your libraries. Make sure to always update all muwerk
libraries.

At the same time make sure that `PubSubClient` stays at Version 2.7. (You can select to use 2.7 if it 
accidentaly got updated.)

Upload the filesystem with configuration information `net.json` and `mqtt.json`
-------------------------------------------------------------------------------

### Plugins for data upload

There are two filesystems to store configuration data. At the time of this writing,
ESP8266 uses the newer and more reliable LittleFS, while ESP32 still uses SPIFFS.

Current (1.8.13) Arduino IDEs have tools for SPIFFS for ESP8266 and ESP32 installed (`ESP32 Sketch Data Upload`). However, muwerk needs Little-FS support for ESP8266.

A general description and installation-instructions for the file-system uploaders can be found here:

* https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html

For ESP8266, you need to additionally install LittleFS support from here:

* https://github.com/earlephilhower/arduino-esp8266littlefs-plugin/releases

### Customize your network-configuration

Open the `data` directory in your project folder and copy the two template files:

* copy `net-default.json` to `net.json`
* copy `mqtt-default.json` to `mqtt.json`

Edit `net.json` and enter die Wifi credtionals. If you want to use an MQTT server,
edit `mqtt.json` and enter the hostname of your mqtt server.

You'll find a complete description of all the configuration options in munet's [README](https://github.com/muwerk/munet#network-configuration).

Once the required json files are configured, use `ESP8266 LittleFS data upload` or `ESP32 Sketch data upload` to upload the configuration to your device.
