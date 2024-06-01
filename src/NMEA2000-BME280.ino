// #define DEBUG_NMEA_MSG // Uncomment to see, what device will send to bus. Use e.g. OpenSkipper or Actisense NMEA Reader  
// #define DEBUG_NMEA_MSG_ASCII // If you want to use simple ascii monitor like Arduino Serial Monitor, uncomment this line
// #define DEBUG_NMEA_Actisense // Uncomment this, so you can test code without CAN bus chips on Arduino Mega

#define DEBUG_MSG

// use the following Pins

#define ESP32_CAN_TX_PIN GPIO_NUM_5  // Set CAN TX port to D5 
#define ESP32_CAN_RX_PIN GPIO_NUM_4  // Set CAN RX port to D4

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <N2kMessages.h>
#include <NMEA2000_CAN.h>
#include <cmath>


#include "common.h"
#include "webhandling.h"
#include "version.h"

bool debugMode = false;
String gStatusSensor;
char Version[] = VERSION; // Manufacturer's Software version code

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


double gTemperature = 0;
double gHumidity = 0;
double gPressure = 0;
double gdewPoint = 0;
double gheatIndex = 0;

// Task handle (Core 0 on ESP32)
TaskHandle_t TaskHandle;

Adafruit_BME280 bme;

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

void CheckN2kSourceAddressChange() {
    uint8_t SourceAddress = NMEA2000.GetN2kSource();

    if (NMEA2000.GetN2kSource(DeviceTemperature) != gN2KSource[DeviceTemperature]) {
        gN2KSource[DeviceTemperature] = NMEA2000.GetN2kSource(DeviceTemperature);
        gSaveParams = true;
    }

    if (NMEA2000.GetN2kSource(DevicePressure) != gN2KSource[DevicePressure]) {
        gN2KSource[DevicePressure] = NMEA2000.GetN2kSource(DevicePressure);
        gSaveParams = true;
    }

    if (NMEA2000.GetN2kSource(DeviceHumidity) != gN2KSource[DeviceHumidity]) {
        gN2KSource[DeviceHumidity] = NMEA2000.GetN2kSource(DeviceHumidity);
        gSaveParams = true;
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

    Serial.printf("Firmware version:%s\n", Version);

    // init wifi
    wifiInit();

    gStatusSensor = "OK";
    if (!bme.begin(BME280_ADDRESS_ALTERNATE)) {
        gStatusSensor = "NOK";
		DEBUG_PRINTLN(F("Could not find a valid BME280 sensor, check wiring!"));
    }
	

    xTaskCreatePinnedToCore(
        loop2, /* Function to implement the task */
        "TaskHandle", /* Name of the task */
        10000,  /* Stack size in words */
        NULL,  /* Task input parameter */
        0,  /* Priority of the task */
        &TaskHandle,  /* Task handle. */
        0 /* Core where the task should run */
    );

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

    // Set device information
    NMEA2000.SetDeviceInformation(
        DeviceId1, // Unique number. Use e.g. Serial number.
        130, // Device function=Devices that measure/report temperature
        75, // Device class=Sensor Communication Interface.
        2046, // Just choosen free from code list on 
        4,  // Marine
        DeviceTemperature
    );

    NMEA2000.SetDeviceInformation(
        DeviceId2, // Unique number. Use e.g. Serial number.
        140, // Device function=Devices that measure/report pressure.
        75, // Device class=Sensor Communication Interface.
        2046, // Just choosen free from code list on 
        4,  // Marine
        DevicePressure
    );

    NMEA2000.SetDeviceInformation(
        DeviceId3, // Unique number. Use e.g. Serial number.
        170, // Device function=Devices that measure/report humidity.
        75, // Device class=Sensor Communication Interface.
        2046, // Just choosen free from code list on 
        4,  // Marine
        DeviceHumidity
    );

    // Disable all msg forwarding to USB (=Serial)
    NMEA2000.EnableForward(false); 

    // If you also want to see all traffic on the bus use N2km_ListenAndNode instead of N2km_NodeOnly below
    NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly);

    NMEA2000.SetN2kSource(gN2KSource[DeviceTemperature], DeviceTemperature);
    NMEA2000.SetN2kSource(gN2KSource[DevicePressure], DevicePressure);
    NMEA2000.SetN2kSource(gN2KSource[DeviceHumidity], DeviceHumidity);


    // Here we tell library, which PGNs we transmit
    NMEA2000.ExtendTransmitMessages(TemperaturTransmitMessages);
    NMEA2000.ExtendTransmitMessages(PressureTransmitMessages);
    NMEA2000.ExtendTransmitMessages(HumidityTransmitMessages);
        
    NMEA2000.Open();
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
        }
        else {
           WebSerial.println(F("Could not find a valid BME280 sensor, check wiring!"));
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
}

void loop2(void* parameter) {
    for (;;) {   // Endless loop
        wifiLoop();

        vTaskDelay(100);
    }
}