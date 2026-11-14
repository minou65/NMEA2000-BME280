// #define DEBUG_WIFI(m) SERIAL_DBG.print(m)

#include <Arduino.h>
#include <WiFi.h>

#include "Config.h"
#include "common.h"
#include "webhandling.h"
#include "favicon.h"
#include <neotimer.h>

#include <DNSServer.h>
#include <IotWebConfAsyncUpdateServer.h>
#include <IotWebRoot.h>
#include <EventLogger.h>

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "A2"

// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN  GPIO_NUM_13

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN LED_BUILTIN

#define ON_LEVEL HIGH


// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "NMEA2000-BME280";

// -- Method declarations.
void handleData(AsyncWebServerRequest* request);
void handleRoot(AsyncWebServerRequest* request);
void handleWebCommand(const String& command);
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

AsyncIotWebConf iotWebConf(thingName, &dnsServer, &asyncWebServerWrapper, wifiInitialApPassword, CONFIG_VERSION);

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
    iotWebConf.setStatusPin(STATUS_PIN, ON_LEVEL);
    iotWebConf.setConfigPin(CONFIG_PIN);
    iotWebConf.setHtmlFormatProvider(&customHtmlFormatProvider);

    SourcesGroup.addItem(&TempSource);
    SourcesGroup.addItem(&HumiditySource);

    iotWebConf.addParameterGroup(&Config);
    iotWebConf.addParameterGroup(&SourcesGroup);

    iotWebConf.addSystemParameter(&APModeOfflineParam);
    
    iotWebConf.setupUpdateServer(
        [](const char* updatePath) {
            AsyncUpdater.onBegin([](const String& message) {
                LOG_INFO("%s", message.c_str());
                });
            AsyncUpdater.onEnd([](const String& message) { LOG_OK("%s", message.c_str()); });
            AsyncUpdater.OnError([](const String& message) {
                LOG_ERROR("%s", message.c_str());
                });
            AsyncUpdater.onProgress([](const String& message) { LOG_INFO("%s", message.c_str()); });
            AsyncUpdater.onLog([](const String& message) { WEB_LOG_INFO("%s", message.c_str()); });
            AsyncUpdater.setup(&server, updatePath);
        },
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
        auto* asyncWebRequestWrapper_ = new AsyncWebRequestWrapper(request);
        iotWebConf.handleConfig(asyncWebRequestWrapper_);
        }
    );

    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response = request->beginResponse_P(200, "image/x-icon", favicon_ico, sizeof(favicon_ico));
        request->send(response);
        }
    );

    server.on("/apple-touch-icon.png", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response = request->beginResponse_P(200, "image/png", favicon_ico, sizeof(favicon_ico));
        request->send(response);
        }
    );

    server.on("/data", HTTP_GET, [](AsyncWebServerRequest* request) { handleData(request); });
    server.onNotFound([](AsyncWebServerRequest* request) {
        AsyncWebRequestWrapper asyncWebRequestWrapper(request);
        iotWebConf.handleNotFound(&asyncWebRequestWrapper);
        }
    );

  // EventLogger Web initialisieren
    bool webOk_ = eventLogger.beginWeb(&server, "/events");
    if (!webOk_) {
        eventLogger.log("EventLogger.beginWeb failed", EventLogger::Status::ERROR, 1);
    }

    eventLogger.setCommandCallback([](const String& command_) {
        handleWebCommand(command_);
        });

    server.onNotFound([](AsyncWebServerRequest* request) {
        AsyncWebRequestWrapper asyncWebRequestWrapper(request);
        iotWebConf.handleNotFound(&asyncWebRequestWrapper);
        }
    );    

    if (APModeOfflineTime > 0) {
        APModeTimer.start(APModeOfflineTime * 60 * 1000);
    }
}

void wifiLoop() {
    iotWebConf.doLoop();

    if (gSaveParams) {
        Config.SetSource(gN2KSource[DeviceTemperature]);
        Config.SetSourceHumidity(gN2KSource[DeviceHumidity]);
        Config.SetSourcePressure(gN2KSource[DevicePressure]);

        iotWebConf.saveConfig();
        gSaveParams = false;
        LOG_OK("Config saved");
    }

    if (APModeTimer.done()) {
        LOG_INFO("AP mode offline time reached");
        iotWebConf.goOffLine();
        APModeTimer.stop();
    }

    if (AsyncUpdater.isFinished()) {
        LOG_OK("Firmware update finished");
        delay(1000);
        ESP.restart();
    }
}

void wifiConnected() {
    WiFi.setSleep(false);
    LOG_INFO("WiFi modem sleep disabled");  
}

void handleData(AsyncWebServerRequest* request) {
	String json_ = "{";
	json_ += "\"rssi\":" + String(WiFi.RSSI()) + ",";
	json_ += "\"Temperature\":" + String(gTemperature, 2) + ",";
	json_ += "\"DewPoint\":" + String(gdewPoint, 2) + ",";
	json_ += "\"HeatIndex\":" + String(gheatIndex, 2) + ",";
	json_ += "\"Pressure\":" + String(gPressure, 2) + ",";
	json_ += "\"Humidity\":" + String(gHumidity, 2);
	json_ += "}";
	request->send(200, "application/json", json_);
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

    content_ += String(F("<link rel=\"icon\" type=\"image/png\" sizes=\"96x96\" href=\"/apple-touch-icon.png\">\n")).c_str();
    content_ += String(F("<link rel=\"apple-touch-icon\" sizes=\"96x96\" href=\"/apple-touch-icon.png\">\n")).c_str();

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
    content_ += fp_.getHtmlTableRowText("Go to <a href = 'config'>configure page</a> to change configuration.").c_str();
    content_ += fp_.getHtmlTableRowText("Go to <a href = 'events'>event log</a> to view the event history.").c_str();
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

void handleWebCommand(const String& command) {
    String cmd_ = command;
    cmd_.trim();
    cmd_.toLowerCase();

    if (cmd_ == "help") {
        WEB_LOG_INFO_AT(0, "=== Available Commands ===");
        WEB_LOG_INFO_AT(1, "help     - Show this help");
        WEB_LOG_INFO_AT(1, "status   - Show system status");
        WEB_LOG_INFO_AT(1, "restart  - Restart device");
        WEB_LOG_INFO_AT(1, "clear    - Clear event log");
        WEB_LOG_INFO_AT(1, "wifi     - Show WiFi info");
        WEB_LOG_INFO_AT(1, "sensor   - Show sensor values");
        return;
    }

    if (cmd_ == "status") {
        WEB_LOG_INFO_AT(0, "=== System Status ===");
        WEB_LOG_INFO_AT(1, "Firmware: %s", Version);
        WEB_LOG_INFO_AT(1, "Board: %s", BOARD_NAME);
        WEB_LOG_INFO_AT(1, "Uptime: %lu s", millis() / 1000);
        WEB_LOG_INFO_AT(1, "Free Heap: %u bytes", ESP.getFreeHeap());
        WEB_LOG_INFO_AT(1, "Sensor Status: %s", gStatusSensor.c_str());
        return;
    }

    if (cmd_ == "restart") {
        WEB_LOG_INFO_AT(0, "Restarting device...");
        delay(1000);
        ESP.restart();
        return;
    }

    if (cmd_ == "clear") {
        eventLogger.clear();
        WEB_LOG_INFO_AT(0, "Event log cleared");
        return;
    }

    if (cmd_ == "wifi") {
        WEB_LOG_INFO_AT(0, "=== WiFi Info ===");
        WEB_LOG_INFO_AT(1, "SSID: %s", WiFi.SSID().c_str());
        WEB_LOG_INFO_AT(1, "IP: %s", WiFi.localIP().toString().c_str());
        WEB_LOG_INFO_AT(1, "MAC: %s", WiFi.macAddress().c_str());
        WEB_LOG_INFO_AT(1, "RSSI: %d dBm", WiFi.RSSI());
        return;
    }

    if (cmd_ == "sensor") {
        WEB_LOG_INFO_AT(0, "=== Sensor Values ===");
        WEB_LOG_INFO_AT(1, "Temperature: %.2f C", gTemperature);
        WEB_LOG_INFO_AT(1, "Humidity: %.2f %%", gHumidity);
        WEB_LOG_INFO_AT(1, "Pressure: %.2f mBar", gPressure);
        WEB_LOG_INFO_AT(1, "Dew Point: %.2f C", gdewPoint);
        WEB_LOG_INFO_AT(1, "Heat Index: %.2f C", gheatIndex);
        WEB_LOG_INFO_AT(1, "Status: %s", gStatusSensor.c_str());
        return;
    }

    WEB_LOG_INFO_AT(0, "Unknown command: %s", cmd_.c_str());
    WEB_LOG_INFO_AT(0, "Type 'help' for available commands");
}

