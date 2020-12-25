// jsonfiles.h - helper functions for SPIFFS filesystem access

#pragma once

// #if defined(__ESP__)

#include "platform.h"
#include "array.h"
#include "map.h"

#include <Arduino_JSON.h>  // Platformio lib no. 6249

namespace ustd {

class ConfigFile {
  public:
    bool init = false;
    String fileName;
    ustd::map<String, String> globalConfig;
    ustd::map<String, ustd::map<String, String>> servicesConfig;

    ConfigFile(String fileName = "/config.json") : fileName(fileName) {
    }

    ~ConfigFile() {
    }

    bool begin(ustd::map<String, String> &userdefaults) {
        SPIFFS.begin();
        checkMigration();
    }

    genUuid(String &uuid) {
    }

    bool fsMigrateConfig() {
    }

    bool checkMigration() {
        bool ret = true;
        fs::File f = SPIFFS.open("\net.json", "r");
        if (f) {
            close(f);
            ret = fsMigrateConfig();
        }
        return ret;
    }

    bool writeJsonObj(String filename, JSONVar jsonobj) {
        if (!init)
            return false;

        fs::File f = SPIFFS.open(filename, "w");
        if (!f) {
            return false;
        }
        String jsonstr = JSON.stringify(jsonobj);
        f.println(jsonstr);
        f.close();
        return true;
    }

    bool readJson(String filename, String &content) {
        if (!fsInitCheck())
            return false;
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

    bool readJsonString(String filename, String key, String &value) {
        String jsonstr;
        if (readJson(filename, jsonstr)) {
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

        bool readNetJsonString(String key, String & value) {
            return readJsonString("/net.json", key, value);
        }

        bool readFriendlyName(String & friendlyName) {
            if (readNetJsonString("friendlyname", friendlyName))
                return true;
            if (readNetJsonString("hostname", friendlyName))
                return true;
            return false;
        }
    };  // ConfigFile class

}  // namespace ustd

// #endif  // defined(__ESP__)
