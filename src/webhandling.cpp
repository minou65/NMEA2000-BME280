// #define DEBUG_WIFI(m) SERIAL_DBG.print(m)

#include <Arduino.h>
#include <ArduinoOTA.h>
#if ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>      
#endif

#include <time.h>
#include <DNSServer.h>
#include <iostream>
#include <string.h>

#include <IotWebConf.h>
#include <IotWebConfESP32HTTPUpdateServer.h>
#include "common.h"
#include "webhandling.h"
#include <N2kMessagesEnumToStr.h>

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "A1"

// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN  -1

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN LED_BUILTIN

#if ESP32 
#define ON_LEVEL HIGH
#else
#define ON_LEVEL LOW
#endif

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "NMEA2000-BME280";

// -- Method declarations.
void handleRoot();
void convertParams();

// -- Callback methods.
void configSaved();
void wifiConnected();

bool gParamsChanged = true;
bool gSaveParams = false;

DNSServer dnsServer;
WebServer server(80);
HTTPUpdateServer httpUpdater;

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);

NMEAConfig Config = NMEAConfig();

iotwebconf::ParameterGroup SourcesGroup = iotwebconf::ParameterGroup("SourcesGroup", "Source");

char TempSourceValue[STRING_LEN];
iotwebconf::SelectParameter TempSource = iotwebconf::SelectParameter("Temperature",
    "TempSource", 
    TempSourceValue,
    STRING_LEN, 
    (char*)TempSourceValues,
    (char*)TempSourceNames,
    sizeof(TempSourceValues) / STRING_LEN,
    STRING_LEN,
    "4" // Main cabin temperature
);

char HumiditySourceValue[STRING_LEN];
iotwebconf::SelectParameter HumiditySource = iotwebconf::SelectParameter("Humidity",
    "HumiditySource",
    HumiditySourceValue,
    STRING_LEN,
    (char*)HumiditySourceValues,
    (char*)HumiditySourceNames,
    sizeof(HumiditySourceValues) / STRING_LEN,
    STRING_LEN,
    "2" // undefined
);

class CustomHtmlFormatProvider : public iotwebconf::HtmlFormatProvider {
protected:
    virtual String getFormEnd() {
        String _s = HtmlFormatProvider::getFormEnd();
        _s += F("</br>Return to <a href='/'>home page</a>.");
        return _s;
    }
};
CustomHtmlFormatProvider customHtmlFormatProvider;

void wifiInit() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("starting up...");


    iotWebConf.setStatusPin(STATUS_PIN, ON_LEVEL);
    iotWebConf.setConfigPin(CONFIG_PIN);
    iotWebConf.setHtmlFormatProvider(&customHtmlFormatProvider);

    SourcesGroup.addItem(&TempSource);
    SourcesGroup.addItem(&HumiditySource);

    iotWebConf.addParameterGroup(&Config);
    iotWebConf.addParameterGroup(&SourcesGroup);
    
    // -- Define how to handle updateServer calls.
    iotWebConf.setupUpdateServer(
        [](const char* updatePath) { httpUpdater.setup(&server, updatePath); },
        [](const char* userName, char* password) { httpUpdater.updateCredentials(userName, password); });

    iotWebConf.setConfigSavedCallback(&configSaved);
    iotWebConf.setWifiConnectionCallback(&wifiConnected);

    iotWebConf.getApTimeoutParameter()->visible = true;

    // -- Initializing the configuration.
    iotWebConf.init();

    convertParams();

    // -- Set up required URL handlers on the web server.
    server.on("/", handleRoot);
    server.on("/config", [] { iotWebConf.handleConfig(); });
    server.onNotFound([]() { iotWebConf.handleNotFound(); });

    Serial.println("Ready.");
}

void wifiLoop() {
    // -- doLoop should be called as frequently as possible.
    iotWebConf.doLoop();
    ArduinoOTA.handle();

    if (gSaveParams) {
        Serial.println(F("Parameters are changed,save them"));

        Config.SetSource(gN2KSource[DeviceTemperature]);
        Config.SetSourceHumidity(gN2KSource[DeviceHumidity]);
        Config.SetSourcePressure(gN2KSource[DevicePressure]);

        iotWebConf.saveConfig();
        gSaveParams = false;
    }
}

void wifiConnected() {
    ArduinoOTA.begin();
}

using namespace std;

void handleRoot() {
    // -- Let IotWebConf test and handle captive portal requests.
    if (iotWebConf.handleCaptivePortal())
    {
        // -- Captive portal request were already served.
        return;
    }

    String page = HTML_Start_Doc;
    
    page.replace("{v}", iotWebConf.getThingName());
    page += "<style>";
    page += ".de{background-color:#ffaaaa;} .em{font-size:0.8em;color:#bb0000;padding-bottom:0px;} .c{text-align: center;} div,input,select{padding:5px;font-size:1em;} input{width:95%;} select{width:100%} input[type=checkbox]{width:auto;scale:1.5;margin:10px;} body{text-align: center;font-family:verdana;} button{border:0;border-radius:0.3rem;background-color:#16A1E7;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%;} fieldset{border-radius:0.3rem;margin: 0px;}";
    // page.replace("center", "left");
    page += ".dot-grey{height: 12px; width: 12px; background-color: #bbb; border-radius: 50%; display: inline-block; }";
    page += ".dot-green{height: 12px; width: 12px; background-color: green; border-radius: 50%; display: inline-block; }";

    page += "</style>";

    page += "<meta http-equiv=refresh content=30 />";
    page += HTML_Start_Body;
    page += "<table border=0 align=center>";
    page += "<tr><td>";

    page += HTML_Start_Fieldset;
    page += HTML_Fieldset_Legend;
    //String Title = String(TempSourceNames[gTempSource]);

    String Title =  N2kEnumTypeToStr(gTempSource);

    page.replace("{l}", Title);
    page += HTML_Start_Table;
        page += "<tr><td align=left>Temperatur: </td><td>" + String(gTemperature) + "&deg;C" + "</td></tr>";
        page += "<tr><td align=left>Dew point: </td><td>" + String(gdewPoint) + "&deg;C" + "</td></tr>";
        page += "<tr><td align=left>Feels like: </td><td>" + String(gheatIndex) + "&deg;C" + "</td></tr>";
        page += "<tr><td align=left>Pressure: </td><td>" + String(gPressure) + "mBar" + "</td></tr>";
        page += "<tr><td align=left>Humidity:</td><td>" + String(gHumidity) + "%" + "</td></tr>";

    page += HTML_End_Table;
    page += HTML_End_Fieldset;

    page += HTML_Start_Fieldset;
    page += HTML_Fieldset_Legend;
    page.replace("{l}", "Network");
    page += HTML_Start_Table;

    page += "<tr><td align=left>MAC Address:</td><td>" + String(WiFi.macAddress()) + "</td></tr>";
    page += "<tr><td align=left>IP Address:</td><td>" + String(WiFi.localIP().toString().c_str()) + "</td></tr>";

    page += HTML_End_Table;
    page += HTML_End_Fieldset;

    page += "<br>";
    page += "<br>";

    page += HTML_Start_Table;
    page += "<tr><td align=left>Go to <a href = 'config'>configure page</a> to change configuration.</td></tr>";
    // page += "<tr><td align=left>Go to <a href='setruntime'>runtime modification page</a> to change runtime data.</td></tr>";
    page += "<tr><td><font size=1>Version: " + String(Version) + "</font></td></tr>";
    page += HTML_End_Table;
    page += HTML_End_Body;

    page += HTML_End_Doc;


    server.send(200, "text/html", page);


}

void convertParams() {
    gTempSource = tN2kTempSource(atoi(TempSourceValue));
    gHumiditySource = tN2kHumiditySource(atoi(HumiditySourceValue));

    gN2KInstance = Config.Instance();
    gN2KSID = Config.SID();

    gN2KSource[DeviceTemperature] = Config.Source();
    gN2KSource[DevicePressure] = Config.SourcePressure();
    gN2KSource[DeviceHumidity] = Config.SourceHumidity();
}

void configSaved() {
    convertParams();
    gParamsChanged = true;
}