// #define DEBUG_WIFI(m) SERIAL_DBG.print(m)

#include <Arduino.h>
#include <ArduinoOTA.h>
#if ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>      
#endif

#include <DNSServer.h>
#include <IotWebConf.h>
#include <IotWebRoot.h>
#include <IotWebConfAsyncClass.h>
#include <IotWebConfAsyncUpdateServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <N2kMessagesEnumToStr.h>

#include "common.h"
#include "webhandling.h"
#include "favicon.h"
#include "neotimer.h"


// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "A2"

// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN  GPIO_NUM_13

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
void handleData(AsyncWebServerRequest* request);
void handleRoot(AsyncWebServerRequest* request);
void convertParams();

// -- Callback methods.
void configSaved();
void wifiConnected();

bool gParamsChanged = true;
bool gSaveParams = false;
uint8_t APModeOfflineTime = 0;

DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebServerWrapper asyncWebServerWrapper(&server);
AsyncUpdateServer AsyncUpdater;
Neotimer APModeTimer = Neotimer();

IotWebConf iotWebConf(thingName, &dnsServer, &asyncWebServerWrapper, wifiInitialApPassword, CONFIG_VERSION);

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

char APModeOfflineValue[STRING_LEN];
iotwebconf::NumberParameter APModeOfflineParam = iotwebconf::NumberParameter("AP offline mode after (minutes)", "APModeOffline", APModeOfflineValue, NUMBER_LEN, "0", "0..30", "min='0' max='30', step='1'");


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

    iotWebConf.addSystemParameter(&APModeOfflineParam);
    
    iotWebConf.setupUpdateServer(
        [](const char* updatePath) { AsyncUpdater.setup(&server, updatePath); },
        [](const char* userName, char* password) { AsyncUpdater.updateCredentials(userName, password); });

    iotWebConf.setConfigSavedCallback(&configSaved);
    iotWebConf.setWifiConnectionCallback(&wifiConnected);

    iotWebConf.getApTimeoutParameter()->visible = true;

    // -- Initializing the configuration.
    iotWebConf.init();

    convertParams();

    // -- Set up required URL handlers on the web server.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) { handleRoot(request); });
    server.on("/config", HTTP_ANY, [](AsyncWebServerRequest* request) {
        AsyncWebRequestWrapper asyncWebRequestWrapper(request);
        iotWebConf.handleConfig(&asyncWebRequestWrapper);
        }
    );
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response = request->beginResponse_P(200, "image/x-icon", favicon_ico, sizeof(favicon_ico));
        request->send(response);
        }
    );
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest* request) { handleData(request); });
    server.onNotFound([](AsyncWebServerRequest* request) {
        AsyncWebRequestWrapper asyncWebRequestWrapper(request);
        iotWebConf.handleNotFound(&asyncWebRequestWrapper);
        }
    );

	WebSerial.begin(&server, "/webserial");

    if (APModeOfflineTime > 0) {
        APModeTimer.start(APModeOfflineTime * 60 * 1000);
    }

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

    if (APModeTimer.done()) {
        Serial.println(F("AP mode offline time reached"));
        iotWebConf.goOffLine();
        APModeTimer.stop();
    }
}

void wifiConnected() {
    ArduinoOTA.begin();
}

void handleData(AsyncWebServerRequest* request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    response->addHeader("Server", "ESP Async Web Server");
    JsonVariant& json_ = response->getRoot();

	json_["rssi"] = WiFi.RSSI();
	json_["Temperature"] = String(gTemperature, 2);
	json_["DewPoint"] = String(gdewPoint, 2);
	json_["HeatIndex"] = String(gheatIndex, 2);
	json_["Pressure"] = String(gPressure, 2);
	json_["Humidity"] = String(gHumidity, 2);

	response->setLength();
	request->send(response);
}

class MyHtmlRootFormatProvider : public HtmlRootFormatProvider {
protected:
    virtual String getScriptInner() {
        String _s = HtmlRootFormatProvider::getScriptInner();
        _s.replace("{millisecond}", "5000");
        _s += F("function updateData(jsonData) {\n");
        _s += F("   document.getElementById('RSSIValue').innerHTML = jsonData.rssi + \"dBm\" \n");
		_s += F("   document.getElementById('TemperaturValue').innerHTML = jsonData.Temperature + \"&deg;C\" \n");
		_s += F("   document.getElementById('DewPointValue').innerHTML = jsonData.DewPoint + \"&deg;C\" \n");
		_s += F("   document.getElementById('HeatIndexValue').innerHTML = jsonData.HeatIndex + \"&deg;C\" \n");
		_s += F("   document.getElementById('PressureValue').innerHTML = jsonData.Pressure + \"mBar\" \n");
		_s += F("   document.getElementById('HumidityValue').innerHTML = jsonData.Humidity + \"%\" \n");

        _s += F("}\n");

        return _s;
    }
};

void handleRoot(AsyncWebServerRequest* request) {
    AsyncWebRequestWrapper asyncWebRequestWrapper(request);
    if (iotWebConf.handleCaptivePortal(&asyncWebRequestWrapper)) {
        return;
    }

    std::string content_;
    MyHtmlRootFormatProvider fp_;

	content_ += fp_.getHtmlHead(iotWebConf.getThingName()).c_str();
	content_ += fp_.getHtmlStyle().c_str();
	content_ += fp_.getHtmlHeadEnd().c_str();
	content_ += fp_.getHtmlScript().c_str();

	content_ += fp_.getHtmlTable().c_str();
	content_ += fp_.getHtmlTableRow().c_str();
	content_ += fp_.getHtmlTableCol().c_str();

	content_ += String(F("<fieldset align=left style=\"border: 1px solid\">\n")).c_str();
	content_ += String(F("<table border=\"0\" align=\"center\" width=\"100%\">\n")).c_str();
	content_ += String(F("<tr><td align=\"left\"> </td></td><td align=\"right\"><span id=\"RSSIValue\">no data</span></td></tr>\n")).c_str();
	content_ += fp_.getHtmlTableEnd().c_str();
	content_ += fp_.getHtmlFieldsetEnd().c_str();

	content_ += fp_.getHtmlFieldset("Temperature").c_str();
	content_ += fp_.getHtmlTable().c_str();
	content_ += fp_.getHtmlTableRowSpan("Temperatur:", "no data", "TemperaturValue").c_str();
	content_ += fp_.getHtmlTableRowSpan("Dew point:", "no data", "DewPointValue").c_str();
	content_ += fp_.getHtmlTableRowSpan("Feels like:", "no data", "HeatIndexValue").c_str();
	content_ += fp_.getHtmlTableRowSpan("Pressure:", "no data", "PressureValue").c_str();
	content_ += fp_.getHtmlTableRowSpan("Humidity:", "no data", "HumidityValue").c_str();
	content_ += fp_.getHtmlTableEnd().c_str();
	content_ += fp_.getHtmlFieldsetEnd().c_str();

	content_ += fp_.getHtmlFieldset("Network").c_str();
	content_ += fp_.getHtmlTable().c_str();
	content_ += fp_.getHtmlTableRowText("MAC Address:", WiFi.macAddress().c_str()).c_str();
	content_ += fp_.getHtmlTableRowText("IP Address:", WiFi.localIP().toString().c_str()).c_str();
	content_ += fp_.getHtmlTableEnd().c_str();
	content_ += fp_.getHtmlFieldsetEnd().c_str();

	content_ += fp_.addNewLine(2).c_str();

	content_ += fp_.getHtmlTable().c_str();

    content_ += fp_.getHtmlTable().c_str();
    content_ += fp_.getHtmlTableRowText("<a href = 'config'>Configuration</a>").c_str();
    content_ += fp_.getHtmlTableRowText("<a href = 'webserial'>Sensor monitoring</a> page.").c_str();
    content_ += fp_.getHtmlTableRowText(fp_.getHtmlVersion(Version)).c_str();
    content_ += fp_.getHtmlTableEnd().c_str();

    content_ += fp_.getHtmlTableColEnd().c_str();
    content_ += fp_.getHtmlTableRowEnd().c_str();
    content_ += fp_.getHtmlTableEnd().c_str();
    content_ += fp_.getHtmlEnd().c_str();

    AsyncWebServerResponse* response = request->beginChunkedResponse("text/html", [content_](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {

        std::string chunk_ = "";
        size_t len_ = min(content_.length() - index, maxLen);
        if (len_ > 0) {
            chunk_ = content_.substr(index, len_ - 1);
            chunk_.copy((char*)buffer, chunk_.length());
        }
        if (index + len_ <= content_.length())
            return chunk_.length();
        else
            return 0;

        });
    response->addHeader("Server", "ESP Async Web Server");
    request->send(response);
}

void convertParams() {
    gTempSource = tN2kTempSource(atoi(TempSourceValue));
    gHumiditySource = tN2kHumiditySource(atoi(HumiditySourceValue));

    gN2KInstance = Config.Instance();
    gN2KSID = Config.SID();

    gN2KSource[DeviceTemperature] = Config.Source();
    gN2KSource[DevicePressure] = Config.SourcePressure();
    gN2KSource[DeviceHumidity] = Config.SourceHumidity();

    APModeOfflineTime = atoi(APModeOfflineValue);
}

void configSaved() {
    convertParams();
    gParamsChanged = true;
}