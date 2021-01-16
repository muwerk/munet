#pragma once

/*! \mainpage munet is a collection of network libraries for ESP8266 and ESP32

\section Introduction

munet implements the following classes based on the cooperative scheduler muwerk:

* * \ref ustd::Net WiFi client and access point connectivity with NTP time synchronization
* * \ref ustd::Ota Over-the-air (OTA) software update
* * \ref ustd::Mqtt Gateway to MQTT server

Libraries are header-only and should work with any c++11 compiler and
and support platforms esp8266 and esp32.

This library requires the libraries ustd, muwerk and requires a
<a href="https://github.com/muwerk/ustd/blob/master/README.md">platform
define</a>.

\section Reference
* * <a href="https://github.com/muwerk/munet">munet github repository</a>

depends on:
* * <a href="https://github.com/muwerk/ustd">ustd github repository</a>
* * <a href="https://github.com/muwerk/muwerk">muwerk github repository</a>

used by:
* * <a href="https://github.com/muwerk/mupplets">mupplets github repository</a>
*/

#include "platform.h"

//! \brief The muwerk namespace
namespace ustd {

// candidate for muwerk utility functions
static String isvalid(String value, unsigned int minlength, String defaultValue) {
    return value.length() < minlength ? defaultValue : value;
}

}  // namespace ustd