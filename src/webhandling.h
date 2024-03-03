// webhandling.h

#ifndef _WEBHANDLING_h
#define _WEBHANDLING_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#if ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>      
#endif

#include <IotWebConf.h>    

#define STRING_LEN 64
#define NUMBER_LEN 5

static char TempSourceValues[][STRING_LEN] = {
    "1",
    "2",
    "3",
    "4",
    "7",
    "8",
    "13",
    "14",
};

static char TempSourceNames[][STRING_LEN] = { 
    "outside", 
    "inside", 
    "engine room", 
    "main cabin", 
    "refridgeration",
    "heating system", 
    "freezer", 
    "exhaust gas"
};

static char HumiditySourceValues[][STRING_LEN] = {
    "1",
    "2",
    "255"
};

static char HumiditySourceNames[][STRING_LEN] = {
    "inside",
    "outside",
    "unknown"
};

#define HTML_Start_Doc "<!DOCTYPE html>\
    <html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>\
    <title>{v}</title>\
";

#define HTML_Start_Body "</head><body>";
#define HTML_Start_Fieldset "<fieldset align=left style=\"border: 1px solid\">";
#define HTML_Start_Table "<table border=0 align=center>";
#define HTML_End_Table "</table>";
#define HTML_End_Fieldset "</fieldset>";
#define HTML_End_Body "</body>";
#define HTML_End_Doc "</html>";
#define HTML_Fieldset_Legend "<legend>{l}</legend>"
#define HTML_Table_Row "<tr><td>{n}</td><td>{v}</td></tr>";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "123456789";

extern void wifiInit();
extern void wifiLoop();

static WiFiClient wifiClient;
extern IotWebConf iotWebConf;

class NMEAConfig : public iotwebconf::ParameterGroup {
public:
    NMEAConfig() : ParameterGroup("nmeaconfig", "NMEA configuration") {
        snprintf(instanceID, STRING_LEN, "%s-instance", this->getId());
        snprintf(sidID, STRING_LEN, "%s-sid", this->getId());
        snprintf(sourceID, STRING_LEN, "%s-source", this->getId());

        this->addItem(&this->InstanceParam);
        this->addItem(&this->SIDParam);

        iotWebConf.addHiddenParameter(&SourceParam);

        // additional sources
        snprintf(sourceIDPressure, STRING_LEN, "%s-sourcepressure", this->getId());
        snprintf(sourceIDHumidity, STRING_LEN, "%s-sourcehumidity", this->getId());

        iotWebConf.addHiddenParameter(&SourcePressureParam);
        iotWebConf.addHiddenParameter(&SourceHumidityParam);

    }

    uint8_t Instance() { return atoi(InstanceValue); };
    uint8_t SID() { return atoi(SIDValue); };
    uint8_t Source() { return atoi(SourceValue); };

    void SetSource(uint8_t source_) {
        String s;
        s = (String)source_;
        strncpy(SourceParam.valueBuffer, s.c_str(), NUMBER_LEN);
    }

    // additional sources
    uint8_t SourcePressure() { return atoi(SourcePressureValue); };
    uint8_t SourceHumidity() { return atoi(SourceHumidityValue); };

    void SetSourcePressure(uint8_t source_) {
        String s;
        s = (String)source_;
        strncpy(SourcePressureParam.valueBuffer, s.c_str(), NUMBER_LEN);
    }

    void SetSourceHumidity(uint8_t source_) {
        String s;
        s = (String)source_;
        strncpy(SourceHumidityParam.valueBuffer, s.c_str(), NUMBER_LEN);
    }
private:
    iotwebconf::NumberParameter InstanceParam = iotwebconf::NumberParameter("Instance", instanceID, InstanceValue, NUMBER_LEN, "255", "1..255", "min='1' max='254' step='1'");
    iotwebconf::NumberParameter SIDParam = iotwebconf::NumberParameter("SID", sidID, SIDValue, NUMBER_LEN, "255", "1..255", "min='1' max='255' step='1'");
    iotwebconf::NumberParameter SourceParam = iotwebconf::NumberParameter("Source", sourceID, SourceValue, NUMBER_LEN, "22", nullptr, nullptr);

    char InstanceValue[NUMBER_LEN];
    char SIDValue[NUMBER_LEN];
    char SourceValue[NUMBER_LEN];


    char instanceID[STRING_LEN];
    char sidID[STRING_LEN];
    char sourceID[STRING_LEN];

    // additional sources
    iotwebconf::NumberParameter SourcePressureParam = iotwebconf::NumberParameter("SourcePressure", sourceIDPressure, SourcePressureValue, NUMBER_LEN, "23", nullptr, nullptr);
    iotwebconf::NumberParameter SourceHumidityParam = iotwebconf::NumberParameter("SourceHumidity", sourceIDHumidity, SourceHumidityValue, NUMBER_LEN, "24", nullptr, nullptr);
    char SourcePressureValue[NUMBER_LEN];
    char SourceHumidityValue[NUMBER_LEN];

    char sourceIDPressure[STRING_LEN];
    char sourceIDHumidity[STRING_LEN];

};

#endif

