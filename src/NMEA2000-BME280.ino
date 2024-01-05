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

uint8_t gN2KSource = 22;
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

char Version[] = "1.0.0.0 (02.01.2024)"; // Manufacturer's Software version code

// List here messages your device will transmit.
const unsigned long TransmitMessages[] PROGMEM = {
    130312L, // Temperature
    130313L, // Humidity
    130314L, // Pressure
    130316L,
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

    if (SourceAddress != gN2KSource) {
#ifdef DEBUG_MSG
        Serial.printf("Address Change: New Address=%d\n", SourceAddress);
#endif // DEBUG_MSG
        gN2KSource = SourceAddress;
        gSaveParams = true;
    }
}

void setup() {
    uint8_t chipid[6];
    uint32_t id = 0;
    int i = 0;

    // Generate unique number from chip id
    esp_efuse_mac_get_default(chipid);
    for (i = 0; i < 6; i++) id += (chipid[i] << (7 * i));

#ifdef DEBUG_MSG
    Serial.begin(115200);

    // wait for serial port to open on native usb devices
    while (!Serial) {
        delay(1);
    }
#endif // DEBUG_MSG

    // init wifi
    wifiInit();


    if (!bme.begin(BME280_ADDRESS_ALTERNATE)) {
#ifdef DEBUG_MSG
        Serial.println("Could not find a valid BME280 sensor, check wiring!");
#endif // DEBUG_MSG
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

    // Reserve enough buffer for sending all messages. This does not work on small memory devices like Uno or Mega
    NMEA2000.SetN2kCANMsgBufSize(8);
    NMEA2000.SetN2kCANReceiveFrameBufSize(150);
    NMEA2000.SetN2kCANSendFrameBufSize(150);

    // Set Product information
    NMEA2000.SetProductInformation(
        "1", // Manufacturer's Model serial code
        100, // Manufacturer's product code
        "BME280",  // Manufacturer's Model ID
        Version,  // Manufacturer's Software version code
        Version // Manufacturer's Model version
    );

    // Set device information
    NMEA2000.SetDeviceInformation(
        id, // Unique number. Use e.g. Serial number.
        130, // Device function=Analog to NMEA 2000 Gateway. See codes on http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf
        75, // Device class=Sensor Communication Interface. See codes on https://web.archive.org/web/20190531120557/https://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf
        2046 // Just choosen free from code list on http://www.nmea.org/Assets/20121020%20nmea%202000%20registration%20list.pdf
    );

#ifdef DEBUG_NMEA_MSG
    Serial.begin(115200);
    NMEA2000.SetForwardStream(&Serial);

#ifdef DEBUG_NMEA_MSG_ASCII
    NMEA2000.SetForwardType(tNMEA2000::fwdt_Text)
#endif // DEBUG_NMEA_MSG_ASCII

#ifdef  DEBUG_NMEA_Actisense
        NMEA2000.SetDebugMode(tNMEA2000::dm_Actisense);
#endif //  DEBUG_NMEA_Actisense

#else
    NMEA2000.EnableForward(false); // Disable all msg forwarding to USB (=Serial)

#endif // DEBUG_NMEA_MSG


    // If you also want to see all traffic on the bus use N2km_ListenAndNode instead of N2km_NodeOnly below
    NMEA2000.SetMode(tNMEA2000::N2km_NodeOnly, gN2KSource);

    // Here we tell library, which PGNs we transmit
    NMEA2000.ExtendTransmitMessages(TransmitMessages);

    NMEA2000.SetOnOpen(OnN2kOpen);
    NMEA2000.Open();
}

void SendN2kTemperature(void) {
    tN2kMsg N2kMsg;
    double Temperature;

    if (TemperatureScheduler.IsTime()) {
        Temperature = bme.readTemperature();
        SetN2kPGN130312(N2kMsg, gN2KSID, gN2KInstance, gTempSource, CToKelvin(Temperature), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg);

        SetN2kPGN130316(N2kMsg, gN2KSID, gN2KInstance, gTempSource, CToKelvin(Temperature), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg);

        gTemperature = Temperature;
    }
}

void SendN2kHumidity(void) {
    tN2kMsg N2kMsg;
    double Humidity;

    if (HumidityScheduler.IsTime()) {
        Humidity = bme.readHumidity();
        SetN2kPGN130313(N2kMsg, gN2KSID, gN2KInstance, gHumiditySource, Humidity, N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg);
        gHumidity = Humidity;
    }
}

void SendN2kPressure(void) {
    tN2kMsg N2kMsg;
    double Pressure;

    if (PressureScheduler.IsTime()) {
        Pressure = bme.readPressure() / 100;  // Read and convert to mBar 
        SetN2kPGN130314(N2kMsg, gN2KSID, gN2KInstance, N2kps_Atmospheric, mBarToPascal(Pressure));
        NMEA2000.SendMsg(N2kMsg);
        gPressure = Pressure;
    }
}

void SendN2KHeatIndexTemperature(double Temperatur_, double Humidity_) {
    tN2kMsg N2kMsg;

    if (HeatIndexScheduler.IsTime()) {
        double _heatIndex = heatIndexCelsius(Temperatur_, Humidity_);
        SetN2kPGN130312(N2kMsg, gN2KSID, gN2KInstance, N2kts_HeatIndexTemperature, CToKelvin(_heatIndex), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg);

        SetN2kPGN130316(N2kMsg, gN2KSID, gN2KInstance, N2kts_HeatIndexTemperature, CToKelvin(_heatIndex), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg);

        gheatIndex = _heatIndex;
    }
}

void SendN2KDewPointTemperature(double Temperatur_, double Humidity_) {
    tN2kMsg N2kMsg;

    if (DewPointScheduler.IsTime()) {
        double _dewPoint = dewPoint(Temperatur_, Humidity_);
        SetN2kPGN130312(N2kMsg, gN2KSID, gN2KInstance, N2kts_DewPointTemperature, CToKelvin(_dewPoint), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg);

        SetN2kPGN130316(N2kMsg, gN2KSID, gN2KInstance, N2kts_DewPointTemperature, CToKelvin(_dewPoint), N2kDoubleNA);
        NMEA2000.SendMsg(N2kMsg);

        gdewPoint = _dewPoint;
    }
}

void loop() {
    SendN2kTemperature();
    SendN2kHumidity();
    SendN2kPressure();

    SendN2KHeatIndexTemperature(gTemperature, gHumidity);
    SendN2KDewPointTemperature(gTemperature, gHumidity);

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