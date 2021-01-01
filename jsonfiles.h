// jsonfiles.h - helper functions for SPIFFS filesystem access

#pragma once

// #if defined(__ESP__)

#include "platform.h"
#include "array.h"
#include "map.h"

#include <Arduino_JSON.h>  // Platformio lib no. 6249

namespace ustd {

bool muFsIsInit = false;

void muSplit(String src, char separator, array<String> *result) {
    int ind;
    String source = src;
    String sb;
    while (true) {
        ind = source.indexOf(separator);
        if (ind == -1) {
            result->add(source);
            return;
        } else {
            sb = source.substring(0, ind);
            result->add(sb);
            source = source.substring(ind + 1);
        }
    }
}

bool muInitFS() {
    bool ret;
#ifdef __USE_SPIFFS_FS__
    ret = SPIFFS.begin(false);
#else
    ret = LittleFS.begin();
#endif
    if (ret) {
        muFsIsInit = true;
    } else {
        muFsIsInit = false;
#ifdef USE_SERIAL_DBG
#ifdef __USE_SPIFFS_FS__
        Serial.println("Failed to initialize SPIFFS filesystem");
#else
        Serial.println("Failed to initialize LittleFS filesystem");
#endif
#endif
    }
    return ret;
}

fs::File muOpen(String filename, String mode) {
    fs::File f;
    if (!muFsIsInit)
        if (!muInitFS())
            return (fs::File)0;
#ifdef __USE_SPIFFS_FS__
    f = SPIFFS.open(filename.c_str(), mode.c_str());
#else
    f = LittleFS.open(filename.c_str(), mode.c_str());
#endif
    if (!f) {
#ifdef USE_SERIAL_DBG
#ifdef __USE_SPIFFS_FS__
        Serial.println("Failed to open " + filename + " on SPIFFS filesystem");
#else
        Serial.println("Failed to open " + filename + " on LittleFS filesystem");
#endif
#endif
    }
    return f;
}

bool muKeyExists(String key) {
    ustd::array<String> keyparts;
    if (key.c_str()[0] == '/') {
        key = key.substring(1);
    }
    muSplit(key, '/', &keyparts);

    // XXX
    return false;
}

String muReadVal(String key, String defaultVal = "") {
    ustd::array<String> keyparts;
    if (key.c_str()[0] == '/') {
        key = key.substring(1);
    }
    muSplit(key, '/', &keyparts);

    if (keyparts.length() < 1) {
#ifdef USE_SERIAL_DBG
        Serial.println("muReadVal key-path too short, minimum needed is filename/topic, got: " +
                       key);
#endif
        return defaultVal;
    }
    String filename = "/" + keyparts[0] + ".json";
    fs::File f = muOpen(filename, "r");
    if (!f) {
#ifdef USE_SERIAL_DBG
        Serial.println("muReadVal file " + filename + " can't be opened.");
#endif
        return defaultVal;
    }
    String jsonstr = "";
    if (!f.available()) {
#ifdef USE_SERIAL_DBG
        Serial.println("Opened " + filename + ", but no data in file!");
#endif
        return defaultVal;
    }
    while (f.available()) {
        // Lets read line by line from the file
        String lin = f.readStringUntil('\n');
        jsonstr = jsonstr + lin;
    }
    f.close();
    JSONVar configObj = JSON.parse(jsonstr);
    if (JSON.typeof(configObj) == "undefined") {
#ifdef USE_SERIAL_DBG
        Serial.println("Parsing input file " + filename + "failed, invalid JSON!");
        Serial.println(jsonstr);
#endif
        return defaultVal;
    }

    JSONVar subobj = configObj;
    for (unsigned int i = 1; i < keyparts.length() - 1; i++) {
        subobj = subobj[keyparts[i]];
        if (JSON.typeof(subobj) == "undefined") {
#ifdef USE_SERIAL_DBG
            Serial.println("From " + key + ", " + keyparts[i] + " not found.");
#endif
            return defaultVal;
        }
        Serial.println("From " + key + ", " + keyparts[i] + " found.");
    }
    String lastKey = keyparts[keyparts.length() - 1];
    String result = "undefined";
    Serial.println("From: " + key + ", last: " + lastKey);
    // JSONVar subobjlst = subobj[lastKey];
    if (!subobj.hasOwnProperty(lastKey)) {
#ifdef USE_SERIAL_DBG
        Serial.println("From " + key + ", last element: " + lastKey + " not found.");
#endif
        return defaultVal;
    } else {
        result = (const char *)subobj[lastKey];
#ifdef USE_SERIAL_DBG
        Serial.println("From " + key + ", last element: " + lastKey + " found: " + result);
#endif
    }

    return result;
}

}  // namespace ustd

// #endif  // defined(__ESP__)
