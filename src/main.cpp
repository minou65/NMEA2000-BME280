// #define DEBUG_NMEA_MSG // Uncomment to see, what device will send to bus. Use e.g. OpenSkipper or Actisense NMEA Reader  
// #define DEBUG_NMEA_MSG_ASCII // If you want to use simple ascii monitor like Arduino Serial Monitor, uncomment this line
// #define DEBUG_NMEA_Actisense // Uncomment this, so you can test code without CAN bus chips on Arduino Mega

#define DEBUG_MSG

#include "Config.h"
#include "common.h"
#include "webhandling.h"

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <N2kMessages.h>
#include <NMEA2000_CAN.h>
#include <cmath>
#include <esp_task_wdt.h>
#include <esp_mac.h>
#include <RebootManager.h>
#include <EventLogger.h>
#include <Preferences.h>

#include <build_version.h>
#include <neotimer.h>

// Forward declarations
void ParseN2kSystemTime(const tN2kMsg& N2kMsg);
void ParseN2kGNSS(const tN2kMsg& N2kMsg);
void loop2(void* parameter);

bool debugMode = false;
String gStatusSensor;
bool gSensorErrorLogged = false;
char Version[] = VERSION_STR; // Manufacturer's Software version code

double gTemperature = 0;
double gHumidity = 0;
double gPressure = 0;
double gdewPoint = 0;
double gheatIndex = 0;

// Task handle (Core 0 on ESP32)
TaskHandle_t TaskHandle;

Adafruit_BME280 bme;

bool TimeSet = false;
unsigned long LastTimeUpdate = 0;

uint8_t gN2KSource[] = { 22, 23, 24 };
uint8_t gN2KInstance = 1;
uint8_t gN2KSID = 1;
tN2kTempSource gTempSource = N2kts_MainCabinTemperature;
tN2kHumiditySource gHumiditySource = N2khs_Undef;

tN2kSyncScheduler TemperatureScheduler(false, 2000, 500);
tN2kSyncScheduler HumidityScheduler(false, 500, 510);
tN2kSyncScheduler PressureScheduler(false, 500, 520);
tN2kSyncScheduler DewPointScheduler(false, 500, 530);
tN2kSyncScheduler HeatIndexScheduler(false, 500, 540);


// Define a function to calculate the dew point
double dewPoint(double temp_celsius, double humidity) {
    const double a = 17.27;
    const double b = 237.7;
    double alpha = ((a * temp_celsius) / (b + temp_celsius)) + log(humidity / 100.0);
    return (b * alpha) / (a - alpha);

}

// This function takes the temperature in Celsius and the relative humidity in percentage
// and returns the heat index temperature in Celsius
double heatIndexCelsius(double temp_celsius, double humidity) {
    // These are the constants used in the formula
    const double c1 = -42.379;
    const double c2 = 2.04901523;
    const double c3 = 10.14333127;
    const double c4 = -0.22475541;
    const double c5 = -0.00683783;
    const double c6 = -0.05481717;
    const double c7 = 0.00122874;
    const double c8 = 0.00085282;
    const double c9 = -0.00000199;

    // Convert the temperature from Celsius to Fahrenheit
    double temp_fahrenheit = temp_celsius * 9.0 / 5.0 + 32;

    // Calculate the heat index in Fahrenheit
    double heat_fahrenheit = c1 + c2 * temp_fahrenheit + c3 * humidity + c4 * temp_fahrenheit * humidity + c5 * temp_fahrenheit * temp_fahrenheit + c6 * humidity * humidity + c7 * temp_fahrenheit * temp_fahrenheit * humidity + c8 * temp_fahrenheit * humidity * humidity + c9 * temp_fahrenheit * temp_fahrenheit * humidity * humidity;

    // Convert the heat index from Fahrenheit to Celsius
    double heat_celsius = (heat_fahrenheit - 32) * 5.0 / 9.0;

    // Return the result
    return heat_celsius;
}

typedef struct {
    unsigned long PGN;
    void (*Handler)(const tN2kMsg& N2kMsg);
} tNMEA2000Handler;

tNMEA2000Handler NMEA2000Handlers[] = {
    {126992L, ParseN2kSystemTime},
    {129029L, ParseN2kGNSS},
    {0,0}
};

// List here messages your device will transmit.
const unsigned long TemperaturTransmitMessages[] PROGMEM = {
    130312L, // Temperature
    130316L,
    0
};

const unsigned long HumidityTransmitMessages[] PROGMEM = {
    130313L, // Humidity
    0
};

const unsigned long PressureTransmitMessages[] PROGMEM = {
    130314L, // Pressure
    0
};

void OnN2kOpen() {
    // Start schedulers now.
    TemperatureScheduler.UpdateNextTime();
    HumidityScheduler.UpdateNextTime();
    PressureScheduler.UpdateNextTime();
    DewPointScheduler.UpdateNextTime();
    HeatIndexScheduler.UpdateNextTime();
}

// Nach den globalen Variablen (nach Zeile 56) hinzuf�gen:

void CheckN2kSourceAddressChange() {
    if (!NMEA2000.ReadResetAddressChanged()) {
        return;
    }

    bool changed_ = false;

    // Check each of the 3 devices for address changes
    for (size_t i_ = 0; i_ < 3; ++i_) {
        uint8_t currentSource_ = NMEA2000.GetN2kSource(i_);
        if (gN2KSource[i_] != currentSource_) {
            LOG_DEBUG("Device %u address changed from %u to %u",
                (unsigned)i_, gN2KSource[i_], currentSource_);
            gN2KSource[i_] = currentSource_;
            changed_ = true;
        }
    }

    // Save all 3 addresses to Preferences
    if (changed_) {
        Preferences prefs_;
        prefs_.begin("n2k", false);

        for (size_t i_ = 0; i_ < 3; ++i_) {
            char key_[16];
            snprintf(key_, sizeof(key_), "N2KSource%u", (unsigned)i_);
            prefs_.putUChar(key_, gN2KSource[i_]);
        }

        LOG_DEBUG("Saved N2K source addresses to Preferences:");
        for (size_t i_ = 0; i_ < 3; ++i_) {
            LOG_DEBUG_AT(1, "N2KSource%u = %u", (unsigned)i_, gN2KSource[i_]);
        }

        prefs_.end();
    }
}

void setup() {
    uint8_t chipid[6];
    uint64_t DeviceId1 = 0;
    uint64_t DeviceId2 = 0;
    uint64_t DeviceId3 = 0;
    int i = 0;

    // Generate unique numbers from chip id
    esp_efuse_mac_get_default(chipid);

    for (int i = 0; i < 6; i++) {
        DeviceId1 += (chipid[i] << (7 * i));
        DeviceId2 += (chipid[i] << (8 * i)); // 8*i statt 7*i
        DeviceId3 += (chipid[i] << (9 * i)); // 9*i statt 7*i oder 8*i
    }

    Serial.begin(115200);
    while (!Serial) {
        delay(1);
    }

    LOG_INFO_AT(0, "========================================");
    LOG_INFO_AT(0, "NMEA2000 BME280 Sensor");
    LOG_INFO_AT(0, "========================================");
    LOG_INFO_AT(1, "Firmware version: %s", Version);
    LOG_INFO_AT(1, "Board: %s", BOARD_NAME);
    LOG_INFO_AT(1, "Sensor type: BME280 (Temperature/Pressure/Humidity)");
    LOG_INFO_AT(0, "========================================");

    RebootManager::begin();
    LOG_INFO_AT(1, "Reboot count: %d", RebootManager::getRebootCount());
    LOG_INFO_AT(1, "Last reboot reason: %s", RebootManager::getLastRebootReasonText().c_str());

    wifiInit();
    LOG_OK("WiFi initialized");

    LOG_INFO_AT(1, "Using N2K addresses: %u (Temp), %u (Pressure), %u (Humidity)",
        gN2KSource[0], gN2KSource[1], gN2KSource[2]);

    gStatusSensor = "OK";
    if (!bme.begin(BME280_I2C_ADDRESS)) {
        gStatusSensor = "NOK";
        LOG_ERROR("ERROR: Could not find a valid BME280 sensor, check wiring");
    } else {
        LOG_OK("BME280 sensor initialized");
    }

    LOG_INFO("Initializing NMEA 2000");

    Preferences prefs_;
    prefs_.begin("n2k", false);

    // MAC-basierte Base-Adresse (wie im DS1820-Projekt)
    uint8_t mac_[6];
    esp_efuse_mac_get_default(mac_);
    uint16_t macSum_ = (uint16_t)mac_[3] + (uint16_t)mac_[4] + (uint16_t)mac_[5];
    uint8_t macBased_ = macSum_ % 20;
    uint8_t baseAddress_ = 20 + (macBased_ * 5);

    for (size_t i_ = 0; i_ < 3; ++i_) {
        char key_[16];
        snprintf(key_, sizeof(key_), "N2KSource%u", (unsigned)i_);

        uint8_t defaultAddr_ = baseAddress_ + i_;
        if (defaultAddr_ > 251) defaultAddr_ = 251;

        uint8_t val_ = prefs_.getUChar(key_, 255);

        if (val_ == 255) {
            // First boot
            gN2KSource[i_] = defaultAddr_;
            prefs_.putUChar(key_, defaultAddr_);
            LOG_INFO_AT(1, "First boot: Set N2KSource%u = %u", (unsigned)i_, defaultAddr_);
        }
        else {
            gN2KSource[i_] = val_;
        }
    }

    prefs_.end();

    // Enable multi device support for 3 devices
    NMEA2000.SetDeviceCount(3); 

    NMEA2000.SetOnOpen(OnN2kOpen);

    // Reserve enough buffer for sending all messages. This does not work on small memory devices like Uno or Mega
    NMEA2000.SetN2kCANMsgBufSize(8);
    NMEA2000.SetN2kCANReceiveFrameBufSize(150);
    NMEA2000.SetN2kCANSendFrameBufSize(150);

    // Set Product information
    NMEA2000.SetProductInformation(
        "101", // Manufacturer's Model serial code
        101, // Manufacturer's product code
        "BME280-TemperaturMonitor",  // Manufacturer's Model ID
        Version,  // Manufacturer's Software version code
        Version, // Manufacturer's Model version
        1, // load equivalency
        0xffff, // NMEA 2000 version - use default
        0xff, // Sertification level - use default
        DeviceTemperature
    );
    LOG_OK_AT(1, "NMEA 2000 Product information for DeviceTemperature set");

    NMEA2000.SetProductInformation(
        "102", // Manufacturer's Model serial code
        102, // Manufacturer's product code
        "BME280-Pressure",  // Manufacturer's Model ID
        Version,  // Manufacturer's Software version code
        Version, // Manufacturer's Model version
        1, // load equivalency
        0xffff, // NMEA 2000 version - use default
        0xff, // Sertification level - use default
        DevicePressure
    );
    LOG_OK_AT(1, "NMEA 2000 Product information for DevicePressure set");

    NMEA2000.SetProductInformation(
        "103", // Manufacturer's Model serial code
        103, // Manufacturer's product code
        "BME280-HumidityMonitor",  // Manufacturer's Model ID
        Version,  // Manufacturer's Software version code
        Version, // Manufacturer's Model version
        1, // load equivalency
        0xffff, // NMEA 2000 version - use default
        0xff, // Sertification level - use default
        DeviceHumidity
    );
    LOG_OK_AT(1, "NMEA 2000 Product information for DeviceHumidity set");

    // Set device information
    NMEA2000.SetDeviceInformation(
        DeviceId1, // Unique number. Use e.g. Serial number.
        130, // Device function=Devices that measure/report temperature
        75, // Device class=Sensor Communication Interface.
        2046, // Just choosen free from code list on 
        4,  // Marine
        DeviceTemperature
    );
    LOG_OK_AT(1, "NMEA 2000 Device information for DeviceTemperature set");

    NMEA2000.SetDeviceInformation(
        DeviceId2, // Unique number. Use e.g. Serial number.
        140, // Device function=Devices that measure/report pressure.
        75, // Device class=Sensor Communication Interface.
        2046, // Just choosen free from code list on 
        4,  // Marine
        DevicePressure
    );
    LOG_OK_AT(1, "NMEA 2000 Device information for DevicePressure set");

    NMEA2000.SetDeviceInformation(
        DeviceId3, // Unique number. Use e.g. Serial number.
        170, // Device function=Devices that measure/report humidity.
        75, // Device class=Sensor Communication Interface.
        2046, // Just choosen free from code list on 
        4,  // Marine
        DeviceHumidity
    );
    LOG_OK_AT(1, "NMEA 2000 Device information for DeviceHumidity set");

    // Disable all msg forwarding to USB (=Serial)
    NMEA2000.EnableForward(false); 

    // If you also want to see all traffic on the bus use N2km_ListenAndNode instead of N2km_NodeOnly below
    NMEA2000.SetMode(tNMEA2000::N2km_ListenAndNode);

    NMEA2000.SetN2kSource(gN2KSource[DeviceTemperature], DeviceTemperature);
    NMEA2000.SetN2kSource(gN2KSource[DevicePressure], DevicePressure);
    NMEA2000.SetN2kSource(gN2KSource[DeviceHumidity], DeviceHumidity);
    LOG_OK_AT(1, "NMEA 2000 N2k sources set");

    // Here we tell library, which PGNs we transmit
    NMEA2000.ExtendTransmitMessages(TemperaturTransmitMessages);
    NMEA2000.ExtendTransmitMessages(PressureTransmitMessages);
    NMEA2000.ExtendTransmitMessages(HumidityTransmitMessages);
        
    NMEA2000.Open();
    LOG_OK("NMEA 2000 initialized");

    xTaskCreatePinnedToCore(
        loop2, /* Function to implement the task */
        "TaskHandle", /* Name of the task */
        10000,  /* Stack size in words */
        NULL,  /* Task input parameter */
        0,  /* Priority of the task */
        &TaskHandle,  /* Task handle. */
        0 /* Core where the task should run */
    );

    esp_task_wdt_add(NULL); //add current thread to WDT watch

    LOG_OK("Setup complete");
}

void SendN2kTemperature(uint8_t instance_) {
    tN2kMsg N2kMsg;
    double Temperature = 0.00;

    if (TemperatureScheduler.IsTime()) {
        TemperatureScheduler.UpdateNextTime();

		if (gStatusSensor != "NOK") {
            Temperature = bme.readTemperature();
		}
        
        SetN2kPGN130312(N2kMsg, gN2KSID, instance_, gTempSource, CToKelvin(Temperature), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg, DeviceTemperature);

        SetN2kPGN130316(N2kMsg, gN2KSID, instance_, gTempSource, CToKelvin(Temperature), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg, DeviceTemperature);

        gTemperature = Temperature;
    }
}

void SendN2kHumidity(uint8_t instance_) {
    tN2kMsg N2kMsg;
    double Humidity = 0.00;

    if (HumidityScheduler.IsTime()) {
        HumidityScheduler.UpdateNextTime();

        if (gStatusSensor != "NOK") {
            Humidity = bme.readHumidity();
        }
        SetN2kPGN130313(N2kMsg, gN2KSID, instance_, gHumiditySource, Humidity, N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg, DeviceHumidity);
        gHumidity = Humidity;
    }
}

void SendN2kPressure(uint8_t instance_) {
    tN2kMsg N2kMsg;
    double Pressure = 0.00;

    if (PressureScheduler.IsTime()) {
        PressureScheduler.UpdateNextTime();
        if (gStatusSensor != "NOK") {
            Pressure = bme.readPressure() / 100.0f;  // Read and convert to mBar 
        } else if (!gSensorErrorLogged) {
            LOG_ERROR("BME280 unavailable: pressure message uses fallback value");
            gSensorErrorLogged = true;
        }
        SetN2kPGN130314(N2kMsg, gN2KSID, instance_, N2kps_Atmospheric, mBarToPascal(Pressure));
        NMEA2000.SendMsg(N2kMsg, DevicePressure);
        gPressure = Pressure;
    }
}

void SendN2KHeatIndexTemperature(double Temperatur_, double Humidity_, uint8_t instance_) {
    tN2kMsg N2kMsg;
	double _heatIndex = 0.00;

    if (HeatIndexScheduler.IsTime()) {
        HeatIndexScheduler.UpdateNextTime();

        if (gStatusSensor != "NOK") {
			_heatIndex = heatIndexCelsius(Temperatur_, Humidity_);
		}

        SetN2kPGN130312(N2kMsg, gN2KSID, instance_, N2kts_HeatIndexTemperature, CToKelvin(_heatIndex), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg, DeviceTemperature);

        SetN2kPGN130316(N2kMsg, gN2KSID, instance_, N2kts_HeatIndexTemperature, CToKelvin(_heatIndex), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg, DeviceTemperature);

        gheatIndex = _heatIndex;
    }
}

void SendN2KDewPointTemperature(double Temperatur_, double Humidity_, uint8_t instance_) {
    tN2kMsg N2kMsg;
	double _dewPoint = 0.00;

    if (DewPointScheduler.IsTime()) {
        DewPointScheduler.UpdateNextTime();
		if (gStatusSensor != "NOK") {
			_dewPoint = dewPoint(Temperatur_, Humidity_);
		}

        SetN2kPGN130312(N2kMsg, gN2KSID, instance_, N2kts_DewPointTemperature, CToKelvin(_dewPoint), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg, DeviceTemperature);

        SetN2kPGN130316(N2kMsg, gN2KSID, instance_, N2kts_DewPointTemperature, CToKelvin(_dewPoint), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg, DeviceTemperature);

        gdewPoint = _dewPoint;
    }
}

void loop() {
    wifiLoop();
    SendN2kTemperature(gN2KInstance);
    SendN2kHumidity(gN2KInstance + 1);
    SendN2kPressure(gN2KInstance + 2);

    SendN2KHeatIndexTemperature(gTemperature, gHumidity, gN2KInstance + 3);
    SendN2KDewPointTemperature(gTemperature, gHumidity, gN2KInstance + 4);

    NMEA2000.ParseMessages();
    CheckN2kSourceAddressChange();

    gParamsChanged = false;

    // Dummy to empty input buffer to avoid board to stuck with e.g. NMEA Reader
    if (Serial.available()) {
        Serial.read();
    }

    esp_task_wdt_reset();
}

void loop2(void* parameter) {
    esp_task_wdt_add(NULL); //add current thread to WDT watch (Core 0)
    for (;;) {   // Endless loop
        

        esp_task_wdt_reset();

        vTaskDelay(1000);
    }
}

void SetSystemTime(uint16_t DaysSince1970, double SecondsSinceMidnight) {
    if (DaysSince1970 == 0xFFFF || SecondsSinceMidnight > 86400) {
        return;
    }

    if (TimeSet && (millis() - LastTimeUpdate < 600000)) {
        return;
    }

    time_t timestamp = (DaysSince1970 * 86400UL) + (time_t)SecondsSinceMidnight;

    struct timeval tv;
    tv.tv_sec = timestamp;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    TimeSet = true;
    LastTimeUpdate = millis();

    char timeStr[64];
    struct tm* timeinfo = localtime(&timestamp);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
    LOG_OK("System time set from NMEA2000: %s UTC", timeStr);
}

void ParseN2kSystemTime(const tN2kMsg& N2kMsg) {
    unsigned char SID;
    uint16_t SystemDate;
    double SystemTime;
    tN2kTimeSource TimeSource;

    if (ParseN2kPGN126992(N2kMsg, SID, SystemDate, SystemTime, TimeSource)) {
        if (TimeSource == N2ktimes_GPS || TimeSource == N2ktimes_GLONASS) {
            SetSystemTime(SystemDate, SystemTime);

            if (debugMode) {
                LOG_OK("Received System Time (PGN 126992): Date=%u, Time=%.1fs, Source=%d",
                    SystemDate, SystemTime, TimeSource);
            }
        }
    }
}

void ParseN2kGNSS(const tN2kMsg& N2kMsg) {
    unsigned char SID;
    uint16_t DaysSince1970;
    double SecondsSinceMidnight;
    double Latitude, Longitude, Altitude;
    tN2kGNSStype GNSStype;
    tN2kGNSSmethod GNSSmethod;
    unsigned char nSatellites;
    double HDOP, PDOP, GeoidalSeparation;
    unsigned char nReferenceStations;
    tN2kGNSStype ReferenceStationType;
    uint16_t ReferenceSationID;
    double AgeOfCorrection;

    if (ParseN2kPGN129029(N2kMsg, SID, DaysSince1970, SecondsSinceMidnight,
        Latitude, Longitude, Altitude, GNSStype, GNSSmethod,
        nSatellites, HDOP, PDOP, GeoidalSeparation,
        nReferenceStations, ReferenceStationType,
        ReferenceSationID, AgeOfCorrection)) {

        SetSystemTime(DaysSince1970, SecondsSinceMidnight);

        if (debugMode) {
            LOG_OK("Received GNSS Time (PGN 129029): Date=%u, Time=%.1fs, Sats=%u",
                DaysSince1970, SecondsSinceMidnight, nSatellites);
        }
    }
}

void handleN2kMessages(const tN2kMsg& N2kMsg_) {
    for (uint8_t i_ = 0; NMEA2000Handlers[i_].PGN != 0; i_++) {
        if (N2kMsg_.PGN == NMEA2000Handlers[i_].PGN) {
            NMEA2000Handlers[i_].Handler(N2kMsg_);
            break;
        }
    }
}