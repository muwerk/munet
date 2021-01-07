// jsonfiles.h - helper functions for SPIFFS filesystem access

#pragma once

// #if defined(__ESP__)

#include "platform.h"
#include "array.h"
#include "map.h"

#include <Arduino_JSON.h>  // Platformio lib no. 6249

#ifndef MAX_FRICKEL_DEPTH
#define MAX_FRICKEL_DEPTH 9
#endif

namespace ustd {

bool muFsIsInit = false;

#ifdef __USE_SPIFFS_FS__
bool muInitFS() {
    if (!muFsIsInit) {
        muFsIsInit = SPIFFS.begin(false);
        if (!muFsIsInit) {
            DBG("Failed to initialize SPIFFS filesystem");
        }
    }
    return muFsIsInit;
}

void muUninitFS() {
    if (muFsIsInit) {
        SPIFFS.end();
        muFsIsInit = false;
    }
}

bool muDelete(String filename) {
    bool ret = SPIFFS.remove(filename);
    if (!ret) {
        DBG("Failed to delete file " + filename);
    }
    return ret;
}

fs::File muOpen(String filename, String mode) {
    if (!muInitFS()) {
        return (fs::File)0;
    }
    fs::File f = SPIFFS.open(filename.c_str(), mode.c_str());
    if (!f) {
        DBG("Failed to open " + filename + " on SPIFFS filesystem");
    }
    return f;
}
#else
bool muInitFS() {
    if (!muFsIsInit) {
        muFsIsInit = LittleFS.begin();
        if (!muFsIsInit) {
            DBG("Failed to initialize LittleFS filesystem");
        }
    }
    return muFsIsInit;
}

void muUninitFS() {
    if (muFsIsInit) {
        LittleFS.end();
        muFsIsInit = false;
    }
}

bool muDelete(String filename) {
    bool ret = LittleFS.remove(filename);
    if (!ret) {
        DBG("Failed to delete file " + filename);
    }
    return ret;
}

fs::File muOpen(String filename, String mode) {
    if (!muInitFS()) {
        return (fs::File)0;
    }
    fs::File f = LittleFS.open(filename.c_str(), mode.c_str());
    if (!f) {
        DBG("Failed to open " + filename + " on LittleFS filesystem");
    }
    return f;
}
#endif

class JsonFile {
  private:
    String filename = "";
    JSONVar obj;
    bool loaded = false;
    bool autocommit;

  public:
    JsonFile(bool autocommit = true) : autocommit(autocommit) {
    }

    void flush() {
        JSONVar newObj;
        loaded = false;
        filename = "";
        obj = newObj;
    }

    bool commit(String name = "commit") {
        if (filename == "") {
            DBG(name + ": cannot commit uninitialited object");
            return false;
        }
        String jsonString = JSON.stringify(obj);

        DBG2(name + ": writing file: " + filename + ", content: " + jsonString);

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

    bool exists(String key, String name = "exists") {
        ustd::array<String> keyparts;
        JSONVar subobj;
        if (prepareRead(key, keyparts, subobj, name)) {
            DBG2(name + ": from " + key + ", element found.");
            return true;
        };
        return false;
    }

    bool readJsonVarArray(String key, ustd::array<JSONVar> &values,
                          String name = "readJsonVarArray") {
        ustd::array<String> keyparts;
        JSONVar subobj;
        if (!prepareRead(key, keyparts, subobj, name)) {
            return false;
        }
        if (JSON.typeof(subobj) != "array") {
            DBG(name + ": from " + key + ", element has wrong type '" + JSON.typeof(subobj) +
                "' - expected 'array'");
            return false;
        }
        values.resize(subobj.length());
        for (int i = 0; i < subobj.length(); i++) {
            JSONVar element(subobj[i]);
            values[i] = element;
        }
        return true;
    }

    bool readBool(String key, bool defaultVal, String name = "readBool") {
        /*! Read a boolean value from a JSON-file. key is an MQTT-topic-like path, structured like
        this: filename/a/b/c/d. This will read the jsonfile /filename.json with example content
        {"a": {"b": {"c": {"d": false}}}}. This will return false, if found, otherwise
        devaultVal.
        @param key combined filename and json-object-path, maxdepth 9.
        @param defaultValue value returned, if key is not found.
        */
        ustd::array<String> keyparts;
        JSONVar subobj;
        if (!prepareRead(key, keyparts, subobj, name)) {
            return defaultVal;
        }
        if (JSON.typeof(subobj) != "boolean") {
            DBG(name + ": from " + key + ", element has wrong type '" + JSON.typeof(subobj) +
                "' - expected 'boolean'");
            return defaultVal;
        }
        bool result = (bool)subobj;
        DBG(name + ": from " + key + ", value: " + (result ? "true" : "false"));
        return result;
    }

    String readString(String key, String defaultVal = "", String name = "readString") {
        /*! Read a string value from a JSON-file. key is an MQTT-topic-like path, structured like
        this: filename/a/b/c/d. This will read the jsonfile /filename.json with example content
        {"a": {"b": {"c": {"d": "some-val"}}}}. This will return "some-val", if found, otherwise
        devaultVal.
        @param key combined filename and json-object-path, maxdepth 9.
        @param defaultValue value returned, if key is not found.
        */
        ustd::array<String> keyparts;
        JSONVar subobj;
        if (!prepareRead(key, keyparts, subobj, name)) {
            return defaultVal;
        }
        if (JSON.typeof(subobj) != "string") {
            DBG(name + ": from " + key + ", element has wrong type '" + JSON.typeof(subobj) +
                "' - expected 'string'");
            return defaultVal;
        }
        String result = (const char *)subobj;
        DBG(name + ": from " + key + ", value: " + result);
        return result;
    }

    double readDouble(String key, double defaultVal, String name = "readDouble") {
        /*! Read a number value from a JSON-file. key is an MQTT-topic-like path, structured like
        this: filename/a/b/c/d. This will read the jsonfile /filename.json with example content
        {"a": {"b": {"c": {"d": 3.14159265359}}}}. This will return 3.14159265359, if found,
        otherwise devaultVal.
        @param key combined filename and json-object-path, maxdepth 9.
        @param defaultValue value returned, if key is not found.
        */
        ustd::array<String> keyparts;
        JSONVar subobj;
        if (!prepareRead(key, keyparts, subobj, name)) {
            return defaultVal;
        }
        if (JSON.typeof(subobj) != "number") {
            DBG(name + ": from " + key + ", element has wrong type '" + JSON.typeof(subobj) +
                "' - expected 'number'");
            return defaultVal;
        }
        double result = (double)subobj;
        DBG(name + ": from " + key + ", value: " + String(result));
        return result;
    }

    long readDouble(String key, long minVal, long maxVal, long defaultVal,
                    String name = "readDouble") {
        /*! Read a number value from a JSON-file. key is an MQTT-topic-like path, structured like
        this: filename/a/b/c/d. This will read the jsonfile /filename.json with example content
        {"a": {"b": {"c": {"d": 3.14159265359}}}}. This will return 3.14159265359, if found,
        otherwise devaultVal.
        @param key combined filename and json-object-path, maxdepth 9.
        @param minVal minimum accepatable value.
        @param maxVal maximum accepatable value.
        @param defaultValue value returned, if key is not found or value outside specified
        bondaries.
        */
        long val = readDouble(key, defaultVal, name);
        return (val < minVal || val > maxVal) ? defaultVal : val;
    }

    long readLong(String key, long defaultVal, String name = "readLong") {
        /*! Read a long integer value from a JSON-file. key is an MQTT-topic-like path, structured
        like this: filename/a/b/c/d. This will read the jsonfile /filename.json with example content
        {"a": {"b": {"c": {"d": 42}}}}. This will return 42, if found, otherwise
        devaultVal. If the value is a number but not an integer, the integer part of the value is
        returned.
        @param key combined filename and json-object-path, maxdepth 9.
        @param defaultValue value returned, if key is not found.
        */
        return (long)readDouble(key, (double)defaultVal);
    }

    long readLong(String key, long minVal, long maxVal, long defaultVal, String name = "readLong") {
        /*! Read a long integer value from a JSON-file. key is an MQTT-topic-like path, structured
        like this: filename/a/b/c/d. This will read the jsonfile /filename.json with example content
        {"a": {"b": {"c": {"d": 42}}}}. This will return 42, if found, otherwise
        devaultVal. If the value is a number but not an integer, the integer part of the value is
        returned.
        @param key combined filename and json-object-path, maxdepth 9.
        @param minVal minimum accepatable value.
        @param maxVal maximum accepatable value.
        @param defaultValue value returned, if key is not found or value outside specified
        bondaries.
        */
        long val = readLong(key, defaultVal, name);
        return (val < minVal || val > maxVal) ? defaultVal : val;
    }

    bool writeString(String key, String val, String name = "writeString") {
        /*! Write a string value to a JSON-file. key is an MQTT-topic-like path, structured like
        this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
        {"a": {"b": {"c": {"d": "content-of-val"}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
        content-of-val.
        @param key combined filename and json-object-path, maxdepth 9.
        @param val value to be written.
        */
        JSONVar target;
        if (!prepareWrite(key, target, name)) {
            return false;
        }
        target = (const char *)val.c_str();
        return autocommit ? commit(name) : true;
    }

    bool writeBool(String key, bool val, String name = "writeBool") {
        /*! Write a boolean value to a JSON-file. key is an MQTT-topic-like path, structured like
        this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
        {"a": {"b": {"c": {"d": true}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
        content-of-val.
        @param key combined filename and json-object-path, maxdepth 9.
        @param val value to be written.
        */
        JSONVar target;
        if (!prepareWrite(key, target, name)) {
            return false;
        }
        target = val;
        return autocommit ? commit(name) : true;
    }

    bool writeDouble(String key, double val, String name = "writeDouble") {
        /*! Write a numerical value to a JSON-file. key is an MQTT-topic-like path, structured like
        this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
        {"a": {"b": {"c": {"d": 3.14159265359}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
        content-of-val.
        @param key combined filename and json-object-path, maxdepth 9.
        @param val value to be written.
        */
        JSONVar target;
        if (!prepareWrite(key, target, name)) {
            return false;
        }
        target = val;
        return autocommit ? commit(name) : true;
    }

    bool writeLong(String key, long val, String name = "wrinteLong") {
        /*! Write a long integer value to a JSON-file. key is an MQTT-topic-like path, structured
        like this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
        {"a": {"b": {"c": {"d": 42}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
        content-of-val.
        @param key combined filename and json-object-path, maxdepth 9.
        @param val value to be written.
        */
        JSONVar target;
        if (!prepareWrite(key, target, name)) {
            return false;
        }
        target = val;
        return autocommit ? commit(name) : true;
    }

  private:
    bool load(String fn, String name) {
        if (fn != filename) {
            filename = fn;
            loaded = false;
        }
        if (loaded) {
            return true;
        }
        filename = fn;
        fs::File f = muOpen(filename, "r");
        if (!f) {
            DBG2(name + ": file " + filename + " can't be opened.");
            return false;
        }
        String jsonstr = "";
        if (!f.available()) {
            DBG2(name + ": opened " + filename + ", but no data in file!");
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
        DBG2(name + ": input file " + filename + " successfully parsed");
        DBG3(name + ": " + jsonstr);
        loaded = true;
        return true;
    }

    bool prepareRead(String key, ustd::array<String> &keyparts, JSONVar &subobj, String name,
                     bool objmode = false) {
        normalize(key);
        split(key, '/', keyparts);
        if (keyparts.length() < (objmode ? 1 : 2)) {
            DBG(name + ": key-path too short, minimum needed is filename/topic, got: " + key);
            return false;
        }
        if (!load("/" + keyparts[0] + ".json", name)) {
            return false;
        }
        JSONVar iterator(obj);
        for (unsigned int i = 1; i < keyparts.length() - 1; i++) {
            JSONVar tmpCopy(iterator[keyparts[i]]);
            iterator = tmpCopy;
            if (JSON.typeof(iterator) == "undefined") {
                DBG2(name + ": from " + key + ", " + keyparts[i] + " not found.");
                return false;
            }
        }
        String lastKey = keyparts[keyparts.length() - 1];
        if (!iterator.hasOwnProperty(lastKey)) {
            DBG2(name + ": from " + key + ", element: " + lastKey + " not found.");
            return false;
        } else {
            JSONVar tmpCopy(iterator[lastKey]);
            subobj = tmpCopy;
        }
        return true;
    }

    bool prepareWrite(String key, JSONVar &target, String name, bool objmode = false) {
        ustd::array<String> keyparts;

        normalize(key);
        split(key, '/', keyparts);
        if (keyparts.length() < (objmode ? 1 : 2)) {
            DBG(name + ": key-path too short, minimum needed is filename/topic, got: " + key);
            return false;
        }
        if (keyparts.length() > MAX_FRICKEL_DEPTH) {
            DBG(name + ": key-path too long, maxdepth is " + MAX_FRICKEL_DEPTH + ", got: " + key);
            return false;
        }
        if (!load("/" + keyparts[0] + ".json", name)) {
            DBG(name + ": creating new.");
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
            target =
                obj[keyparts[1]][keyparts[2]][keyparts[3]][keyparts[4]][keyparts[5]][keyparts[6]];
            return true;
        case 8:
            target = obj[keyparts[1]][keyparts[2]][keyparts[3]][keyparts[4]][keyparts[5]]
                        [keyparts[6]][keyparts[7]];
            return true;
        case 9:
            target = obj[keyparts[1]][keyparts[2]][keyparts[3]][keyparts[4]][keyparts[5]]
                        [keyparts[6]][keyparts[7]][keyparts[8]];
            return true;
        default:
            DBG(name +
                ": SERIOUS PROGRAMMING ERROR - MAX_FRICKEL_DEV higher than implemented support "
                "in " +
                __FILE__ + " line number " + String(__LINE__));
            return false;
        }
    }

    static void normalize(String &src) {
        if (src.c_str()[0] == '/') {
            src = src.substring(1);
        }
    }

  public:
    static void split(String &src, char separator, array<String> &result) {
        int ind;
        String source = src;
        String sb;
        while (true) {
            ind = source.indexOf(separator);
            if (ind == -1) {
                result.add(source);
                return;
            } else {
                sb = source.substring(0, ind);
                result.add(sb);
                source = source.substring(ind + 1);
            }
        }
    }
};

void muSplit(String src, char separator, array<String> *result) {
    JsonFile::split(src, separator, *result);
}

bool muKeyExists(String key) {
    JsonFile jf;
    return jf.exists(key, "muKeyExists");
}

String muReadString(String key, String defaultVal = "") {
    /*! Read a string value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": "some-val"}}}}. This will return "some-val", if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    JsonFile jf;
    return jf.readString(key, defaultVal, "muReadString");
}

String muReadVal(String key, String defaultVal = "") {
    /*! Read a string value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": "some-val"}}}}. This will return "some-val", if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    JsonFile jf;
    return jf.readString(key, defaultVal, "muReadVal");
}

bool muReadBool(String key, bool defaultVal) {
    /*! Read a boolean value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": false}}}}. This will return false, if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    JsonFile jf;
    return jf.readBool(key, defaultVal, "muReadBool");
}

bool muReadVal(String key, bool defaultVal) {
    /*! Read a boolean value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": false}}}}. This will return false, if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    JsonFile jf;
    return jf.readBool(key, defaultVal, "muReadVal");
}

double muReadDouble(String key, double defaultVal) {
    /*! Read a number value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": 3.14159265359}}}}. This will return 3.14159265359, if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    JsonFile jf;
    return jf.readDouble(key, defaultVal, "muReadDouble");
}

double muReadVal(String key, double defaultVal) {
    /*! Read a number value from a JSON-file. key is an MQTT-topic-like path, structured like this:
    filename/a/b/c/d. This will read the jsonfile /filename.json with example content
    {"a": {"b": {"c": {"d": 3.14159265359}}}}. This will return 3.14159265359, if found, otherwise
    devaultVal.
    @param key combined filename and json-object-path, maxdepth 9.
    @param defaultValue value returned, if key is not found.
    */
    JsonFile jf;
    return jf.readDouble(key, defaultVal, "muReadVal");
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

bool muWriteString(String key, String val) {
    /*! Write a string value to a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": "content-of-val"}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    JsonFile jf;
    return jf.writeString(key, val, "muWriteString");
}

bool muWriteVal(String key, String val) {
    /*! Write a string value to a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": "content-of-val"}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    return muWriteString(key, val);
}

bool muWriteBool(String key, bool val) {
    /*! Write a boolean value to a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": true}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    JsonFile jf;
    return jf.writeBool(key, val, "muWriteBool");
}

bool muWriteVal(String key, bool val) {
    /*! Write a boolean value to a JSON-file. key is an MQTT-topic-like path, structured like
    this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
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
    JsonFile jf;
    return jf.writeDouble(key, val, "muWriteDouble");
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
    /*! Write a long integer value to a JSON-file. key is an MQTT-topic-like path, structured
    like this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": 42}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    JsonFile jf;
    return jf.writeLong(key, val, "muWriteLong");
}

bool muWriteVal(String key, long val) {
    /*! Write a long integer value to a JSON-file. key is an MQTT-topic-like path, structured
    like this: filename/a/b/c/d. This will write the jsonfile /filename.json with content
    {"a": {"b": {"c": {"d": 42}}}}. Use muReadVal("filename/a/b/c/d") to retrieve
    content-of-val.
    @param key combined filename and json-object-path, maxdepth 9.
    @param val value to be written.
    */
    return muWriteLong(key, val);
}
}  // namespace ustd

// #endif  // defined(__ESP__)
