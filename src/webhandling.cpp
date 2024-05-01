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
#include "IotWebRoot.h"

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
void handleData();
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
    server.on("/data", HTTP_GET, []() { handleData(); });
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

void handleData() {
    String _json = "{";
        _json += "\"rssi\":\"" + String(WiFi.RSSI()) + "\",";
    	_json += "\"Temperature\":\"" + String(gTemperature) + "\",";
        _json += "\"DewPoint\":\"" + String(gdewPoint) + "\",";
        _json += "\"HeatIndex\":\"" + String(gheatIndex) + "\",";
        _json += "\"Pressure\":\"" + String(gPressure) + "\",";
        _json += "\"Humidity\":\"" + String(gHumidity) + "\"";
    _json += "}";
    server.send(200, "text/plain", _json);
}

class MyHtmlRootFormatProvider : public HtmlRootFormatProvider {
protected:
    virtual String getScriptInner() {
        String _s = HtmlRootFormatProvider::getScriptInner();
        _s.replace("{millisecond}", "5000");
        _s += F("function updateData(jsonData) {\n");
        _s += F("   document.getElementById('RSSIValue').innerHTML = jsonData.rssi + \"dBm\" \n");
		_s += F("   document.getElementById('TemperaturValue').innerHTML = jsonData.Temperature + \"&deg;\" \n");
		_s += F("   document.getElementById('DewPointValue').innerHTML = jsonData.DewPoint + \"&deg;\" \n");
		_s += F("   document.getElementById('HeatIndexValue').innerHTML = jsonData.HeatIndex + \"&deg;\" \n");
		_s += F("   document.getElementById('PressureValue').innerHTML = jsonData.Pressure + \"mBar\" \n");
		_s += F("   document.getElementById('HumidityValue').innerHTML = jsonData.Humidity + \"%\" \n");

        _s += F("}\n");

        return _s;
    }
};

void handleRoot() {
    // -- Let IotWebConf test and handle captive portal requests.
    if (iotWebConf.handleCaptivePortal()){
        return;
    }

    MyHtmlRootFormatProvider rootFormatProvider;

    String _response = "";
    _response += rootFormatProvider.getHtmlHead(iotWebConf.getThingName());
    _response += rootFormatProvider.getHtmlStyle();
    _response += rootFormatProvider.getHtmlHeadEnd();
    _response += rootFormatProvider.getHtmlScript();

    _response += rootFormatProvider.getHtmlTable();
    _response += rootFormatProvider.getHtmlTableRow() + rootFormatProvider.getHtmlTableCol();

    _response += F("<fieldset align=left style=\"border: 1px solid\">\n");
    _response += F("<table border=\"0\" align=\"center\" width=\"100%\">\n");
    _response += F("<tr><td align=\"left\"> </td></td><td align=\"right\"><span id=\"RSSIValue\">no data</span></td></tr>\n");
    _response += rootFormatProvider.getHtmlTableEnd();
    _response += rootFormatProvider.getHtmlFieldsetEnd();

	_response += rootFormatProvider.getHtmlFieldset("Temperature");
	_response += rootFormatProvider.getHtmlTable();
	_response += rootFormatProvider.getHtmlTableRowSpan("Temperatur", "TemperaturValue", "no data");
	_response += rootFormatProvider.getHtmlTableRowSpan("Dew point", "DewPointValue", "no data");
	_response += rootFormatProvider.getHtmlTableRowSpan("Feels like", "HeatIndexValue", "no data");
	_response += rootFormatProvider.getHtmlTableRowSpan("Pressure", "PressureValue", "no data");
	_response += rootFormatProvider.getHtmlTableRowSpan("Humidity", "HumidityValue", "no data");
	_response += rootFormatProvider.getHtmlTableEnd();
	_response += rootFormatProvider.getHtmlFieldsetEnd();

    _response += rootFormatProvider.getHtmlFieldset("Network");
    _response += rootFormatProvider.getHtmlTable();
    _response += rootFormatProvider.getHtmlTableRowText("MAC Address:", WiFi.macAddress());
    _response += rootFormatProvider.getHtmlTableRowText("IP Address:", WiFi.localIP().toString().c_str());
    _response += rootFormatProvider.getHtmlTableEnd();
    _response += rootFormatProvider.getHtmlFieldsetEnd();

    _response += rootFormatProvider.addNewLine(2);

    _response += rootFormatProvider.getHtmlTable();
    _response += rootFormatProvider.getHtmlTableRowText("Go to <a href = 'config'>configure page</a> to change configuration.");
    _response += rootFormatProvider.getHtmlTableRowText(rootFormatProvider.getHtmlVersion(Version));
    _response += rootFormatProvider.getHtmlTableEnd();

    _response += rootFormatProvider.getHtmlTableColEnd() + rootFormatProvider.getHtmlTableRowEnd();
    _response += rootFormatProvider.getHtmlTableEnd();
    _response += rootFormatProvider.getHtmlEnd();

	server.send(200, "text/html", _response);
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