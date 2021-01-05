// jsonfiles.h - helper functions for SPIFFS filesystem access

#pragma once

// #if defined(__ESP__)

#include "platform.h"
#include "array.h"
#include "map.h"

#include <Arduino_JSON.h>  // Platformio lib no. 6249

#ifndef DBG
#ifdef USE_SERIAL_DBG
#define DBG_ONLY(f) f
#define DBG(f) Serial.println(f)
#define DBGF(...) Serial.printf(__VA_ARGS__)
#if USE_SERIAL_DBG > 1
#define DBG2(f) Serial.println(f)
#else
#define DBG2(f)
#endif
#else
#define DBG_ONLY(f)
#define DBG(f)
#define DBG2(f)
#define DBGF(...)
#endif
#endif

#ifndef MAX_FRICKEL_DEPTH
#define MAX_FRICKEL_DEPTH 9
#endif

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
#ifdef __USE_SPIFFS_FS__
        DBG("Failed to initialize SPIFFS filesystem");
#else
        DBG("Failed to initialize LittleFS filesystem");
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
#ifdef __USE_SPIFFS_FS__
        DBG("Failed to open " + filename + " on SPIFFS filesystem");
#else
        DBG("Failed to open " + filename + " on LittleFS filesystem");
#endif
    }
    return f;
}

bool _muFsPrepareRead(String key, String &filename, ustd::array<String> &keyparts, JSONVar &obj,
                      JSONVar &subobj, String &lastKey, String name, bool objmode = false) {
    if (key.c_str()[0] == '/') {
        key = key.substring(1);
    }
    muSplit(key, '/', &keyparts);

    if (keyparts.length() < (objmode ? 1 : 2)) {
        DBG(name + ": key-path too short, minimum needed is filename/topic, got: " + key);
        return false;
    }
    filename = "/" + keyparts[0] + ".json";
    fs::File f = muOpen(filename, "r");
    if (!f) {
        DBG(name + ": file " + filename + " can't be opened.");
        return false;
    }
    String jsonstr = "";
    if (!f.available()) {
        DBG(name + ": opened " + filename + ", but no data in file!");
        return false;
    }
    while (f.available()) {
        // Lets read line by line from the file
        String lin = f.readStringUntil('\n');
        jsonstr = jsonstr + lin;
    }
    f.close();
    obj = JSON.parse(jsonstr);
    if (JSON.typeof(obj) == "undefined") {
        DBG(name + ": parsing input file " + filename + "failed, invalid JSON!");
        DBG2(name + ": " + jsonstr);
        return false;
    }
    DBG(name + ": input file " + filename + " successfully parsed");
    DBG2(name + ": " + jsonstr);
    JSONVar iterator(obj);
    for (unsigned int i = 1; i < keyparts.length() - 1; i++) {
        JSONVar tmpCopy(iterator[keyparts[i]]);
        iterator = tmpCopy;
        if (JSON.typeof(iterator) == "undefined") {
            DBG(name + ": from " + key + ", " + keyparts[i] + " not found.");
            return false;
        }
        DBG(name + ": from " + key + ", " + keyparts[i] + " found.");
    }
    lastKey = keyparts[keyparts.length() - 1];
    if (!iterator.hasOwnProperty(lastKey)) {
        DBG(name + ": from " + key + ", last element: " + lastKey + " not found.");
        return false;
    } else {
        JSONVar tmpCopy(iterator[lastKey]);
        subobj = tmpCopy;
    }
    return true;
}

bool muKeyExists(String key) {
    ustd::array<String> keyparts;
    String filename, lastKey;
    JSONVar obj, subobj;
    if (_muFsPrepareRead(key, filename, keyparts, obj, subobj, lastKey, "muKeyExists")) {
        DBG("muKeyExists: from " + key + ", last element: " + lastKey + "found.");
        return true;
    };
    return false;
}

String muReadString(String key, String defaultVal = "") {
    /*! Read a string value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": "some-val"}}}}. This will return "some-val", if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    ustd::array<String> keyparts;
    String filename, lastKey;
    JSONVar obj, subobj;
    if (!_muFsPrepareRead(key, filename, keyparts, obj, subobj, lastKey, "muReadVal")) {
        return defaultVal;
    }
    if (JSON.typeof(subobj) != "string") {
        DBG("muReadVal: from " + key + ", last element: " + lastKey + " has wrong type '" +
            JSON.typeof(subobj) + "' - expected 'string'");
        return defaultVal;
    }
    String result = (const char *)subobj;
    DBG("muReadVal: from " + key + ", last element: " + lastKey + " found: " + result);
    return result;
}
String muReadVal(String key, String defaultVal = "") {
    /*! Read a string value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": "some-val"}}}}. This will return "some-val", if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    return muReadString(key, defaultVal);
}

bool muReadBool(String key, bool defaultVal) {
    /*! Read a boolean value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": false}}}}. This will return false, if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    ustd::array<String> keyparts;
    String filename, lastKey;
    JSONVar obj, subobj;
    if (!_muFsPrepareRead(key, filename, keyparts, obj, subobj, lastKey, "muReadVal")) {
        return defaultVal;
    }
    if (JSON.typeof(subobj) != "boolean") {
        DBG("muReadVal: from " + key + ", last element: " + lastKey + " has wrong type '" +
            JSON.typeof(subobj) + "' - expected 'boolean'");
        return defaultVal;
    }
    bool result = (bool)subobj;
    DBG("muReadVal: from " + key + ", last element: " + lastKey +
        " found: " + (result ? "true" : "false"));
    return result;
}

bool muReadVal(String key, bool defaultVal) {
    /*! Read a boolean value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": false}}}}. This will return false, if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    return muReadBool(key, defaultVal);
}

double muReadDouble(String key, double defaultVal) {
    /*! Read a number value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": 3.14159265359}}}}. This will return 3.14159265359, if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    ustd::array<String> keyparts;
    String filename, lastKey;
    JSONVar obj, subobj;
    if (!_muFsPrepareRead(key, filename, keyparts, obj, subobj, lastKey, "muReadVal")) {
        return defaultVal;
    }
    if (JSON.typeof(subobj) != "number") {
        DBG("muReadVal: from " + key + ", last element: " + lastKey + " has wrong type '" +
            JSON.typeof(subobj) + "' - expected 'number'");
        return defaultVal;
    }
    double result = (double)subobj;
    DBG("muReadVal: from " + key + ", last element: " + lastKey + " found: " + String(result));
    return result;
}

double muReadVal(String key, double defaultVal) {
    /*! Read a number value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": 3.14159265359}}}}. This will return 3.14159265359, if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    return muReadDouble(key, defaultVal);
}

long muReadLong(String key, long defaultVal) {
    /*! Read an long integer value from a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": 42}}}}. This will return 42, if found, otherwise
    devaultVal. If the value is a number but not an integer, the integer part of the value is
    returned.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    return (long)muReadDouble(key, (double)defaultVal);
}

long muReadVal(String key, long defaultVal) {
    /*! Read an long integer value from a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": 42}}}}. This will return 42, if found, otherwise
    devaultVal. If the value is a number but not an integer, the integer part of the value is
    returned.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    return (long)muReadDouble(key, (double)defaultVal);
}

bool _muFsPrepareWrite(String key, String &filename, JSONVar &obj, JSONVar &target, String name,
                       bool objmode = false) {
    if (key.c_str()[0] == '/') {
        key = key.substring(1);
    }
    ustd::array<String> keyparts;
    muSplit(key, '/', &keyparts);
    if (keyparts.length() < (objmode ? 1 : 2)) {
        DBG(name + ": key-path too short, minimum needed is filename/topic, got: " + key);
        return false;
    }
    if (keyparts.length() > MAX_FRICKEL_DEPTH) {
        DBG(name + ": key-path too long, maxdepth is " + MAX_FRICKEL_DEPTH + ", got: " + key);
        return false;
    }
    filename = "/" + keyparts[0] + ".json";
    fs::File f = muOpen(filename, "r");
    if (!f) {
        DBG(name + ": file " + filename + " can't be opened, creating new.");
    } else {
        String jsonstr = "";
        if (!f.available()) {
            DBG(name + ": opened " + filename + ", but no data in file, creating new.");
        } else {
            while (f.available()) {
                // Lets read line by line from the file
                String lin = f.readStringUntil('\n');
                jsonstr = jsonstr + lin;
            }
            f.close();
            JSONVar configObj = JSON.parse(jsonstr);
            if (JSON.typeof(configObj) == "undefined") {
                DBG(name + ": parsing input file " + filename + "failed, invalid JSON!");
                DBG2(jsonstr);
            } else {
                obj = configObj;
            }
        }
    }
    // frickel
    switch (keyparts.length()) {
    case 1:
        // possible only in object mode
        target = obj;
        return true;
    case 2:
        target = obj[keyparts[1]];
        return true;
    case 3:
        target = obj[keyparts[1]][keyparts[2]];
        return true;
    case 4:
        target = obj[keyparts[1]][keyparts[2]][keyparts[3]];
        return true;
    case 5:
        target = obj[keyparts[1]][keyparts[2]][keyparts[3]][keyparts[4]];
        return true;
    case 6:
        target = obj[keyparts[1]][keyparts[2]][keyparts[3]][keyparts[4]][keyparts[5]];
        return true;
    case 7:
        target = obj[keyparts[1]][keyparts[2]][keyparts[3]][keyparts[4]][keyparts[5]][keyparts[6]];
        return true;
    case 8:
        target = obj[keyparts[1]][keyparts[2]][keyparts[3]][keyparts[4]][keyparts[5]][keyparts[6]]
                    [keyparts[7]];
        return true;
    case 9:
        target = obj[keyparts[1]][keyparts[2]][keyparts[3]][keyparts[4]][keyparts[5]][keyparts[6]]
                    [keyparts[7]][keyparts[8]];
        return true;
    default:
        DBG(name +
            ": SERIOUS PROGRAMMING ERROR - MAX_FRICKEL_DEV higher than implemented support in " +
            __FILE__ + " line number " + String(__LINE__));
        return false;
    }
}

bool _muFsFinishWrite(String filename, JSONVar &obj, String name) {
    String jsonString = JSON.stringify(obj);

    DBG(name + ": file: " + filename + ", content: " + jsonString);

    fs::File f = muOpen(filename, "w");
    if (!f) {
        DBG(name + ": file " + filename + " can't be opened for write, failure.");
        return false;
    } else {
        f.print(jsonString.c_str());
        f.close();
        return true;
    }
}

bool muWriteString(String key, String val) {
    /*! Write a string value to a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": "content-of-val"}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    String filename;
    JSONVar obj, target;

    if (!_muFsPrepareWrite(key, filename, obj, target, "muWriteVal")) {
        return false;
    }

    target = (const char *)val.c_str();

    return _muFsFinishWrite(filename, obj, "muWriteVal");
}

bool muWriteVal(String key, String val) {
    /*! Write a string value to a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": "content-of-val"}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    return muWriteString(key, val);
}

bool muWriteBool(String key, bool val) {
    /*! Write a boolean value to a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": true}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    String filename;
    JSONVar obj, target;

    if (!_muFsPrepareWrite(key, filename, obj, target, "muWriteVal")) {
        return false;
    }

    target = val;

    return _muFsFinishWrite(filename, obj, "muWriteVal");
}

bool muWriteVal(String key, bool val) {
    /*! Write a boolean value to a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": true}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    return muWriteBool(key, val);
}

bool muWriteDouble(String key, double val) {
    /*! Write a numerical value to a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": 3.14159265359}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    String filename;
    JSONVar obj, target;

    if (!_muFsPrepareWrite(key, filename, obj, target, "muWriteVal")) {
        return false;
    }

    target = val;

    return _muFsFinishWrite(filename, obj, "muWriteVal");
}

bool muWriteVal(String key, double val) {
    /*! Write a numerical value to a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": 3.14159265359}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    return muWriteDouble(key, val);
}

bool muWriteLong(String key, long val) {
    /*! Write a long integer value to a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": 42}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    String filename;
    JSONVar obj, target;

    if (!_muFsPrepareWrite(key, filename, obj, target, "muWriteVal")) {
        return false;
    }

    target = val;

    return _muFsFinishWrite(filename, obj, "muWriteVal");
}

bool muWriteVal(String key, long val) {
    /*! Write a long integer value to a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": 42}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    return muWriteLong(key, val);
}
}  // namespace ustd

// #endif  // defined(__ESP__)
