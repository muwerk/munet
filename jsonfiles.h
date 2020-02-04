// jsonfiles.h - helper functions for SPIFFS filesystem access

#pragma once


// #if defined(__ESP__)

#include "platform.h"
#include "array.h"
#include "map.h"

#include <Arduino_JSON.h>  // Platformio lib no. 6249

namespace ustd {

bool fsBeginDone=false;

bool fsInitCheck() {
    if (!spiffsBeginDone) {
        SPIFFS.begin();
        spiffsBeginDone=true;
    }
    return true;
}

bool writeJson(String filename, JSONVar jsonobj) {
    if (!fsInitCheck()) return false;
    fs::File f = SPIFFS.open(filename, "w");
    if (!f) {
        return false;
    }
    String jsonstr=JSON.stringify(jsonobj);
    f.println(jsonstr);
    f.close();
    return true;
}

bool readJson(String filename, String& content) {
    if (!fsInitCheck()) return false;
    content = "";
    fs::File f = SPIFFS.open(filename, "r");
    if (!f) {
        return false;
    } else {
        while (f.available()) {
            String lin = f.readStringUntil('\n');
            content = content + lin;
        }
        f.close();
    }
    return true;
}

bool readNetJsonString(String key, String& value) {
    String jsonstr;
    if (readJson("/net.json", jsonstr)) {
        JSONVar configObj = JSON.parse(jsonstr);
        if (JSON.typeof(configObj) == "undefined") {
            return false;
        }
        if (configObj.hasOwnProperty(key.c_str())) {
            value = (const char *)configObj[key.c_str()];
        } else {
            return false;
        }
        return true;
    } else {
        return false;
    }
}


bool readFriendlyName(String& friendlyName) {
    if (readNetJsonString("friendlyname",friendlyName)) return true;
    if (readNetJsonString("hostname",friendlyName)) return true;
    return false;
}

}  // namespace ustd

// #endif  // defined(__ESP__)
