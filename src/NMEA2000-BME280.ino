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

#include "common.h"
#include "webhandling.h"

uint8_t gN2KSource = 25;
tN2kTempSource gTempSource = N2kts_MainCabinTemperature;

// Set time offsets
#define SlowDataUpdatePeriod 1000  // Time between CAN Messages sent
#define TempSendOffset 0           // + 0 ms
#define HumiditySendOffset 50      // + 50 ms
#define PressureSendOffset 100     // + 100 ms


// Define the constants for the Magnus formula
#define a 17.62
#define b 243.12

// Define a function to calculate the alpha value
double alpha(double T, double RH) {
    return a * T / (b + T) + log(RH);
}

// Define a function to calculate the dew point
double dewPoint(double T, double RH) {
    return b * alpha(T, RH) / (a - alpha(T, RH));
}


double gTemperature = 0;
double gHumidity = 0;
double gPressure = 0;
double gdewPoint = 0;

// Task handle (Core 0 on ESP32)
TaskHandle_t TaskHandle;

Adafruit_BME280 bme;

char Version[] = "0.0.0.1 (2023-08-26)"; // Manufacturer's Software version code

// List here messages your device will transmit.
const unsigned long TransmitMessages[] PROGMEM = {
    130312L, // Temperature
    130313L, // Humidity
    130314L, // Pressure
    0
};

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
        132, // Device function=Analog to NMEA 2000 Gateway. See codes on http://www.nmea.org/Assets/20120726%20nmea%202000%20class%20&%20function%20codes%20v%202.00.pdf
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


    NMEA2000.Open();
}

bool IsTimeToUpdate(unsigned long NextUpdate) {
    return (NextUpdate < millis());
}

unsigned long InitNextUpdate(unsigned long Period, unsigned long Offset = 0) {
    return millis() + Period + Offset;
}

void SetNextUpdate(unsigned long& NextUpdate, unsigned long Period) {
    while (NextUpdate < millis()) NextUpdate += Period;
}

void SendN2kTemperature(void) {
    static unsigned long SlowDataUpdated = InitNextUpdate(SlowDataUpdatePeriod, TempSendOffset);
    tN2kMsg N2kMsg;
    double Temperature;

    if (IsTimeToUpdate(SlowDataUpdated)) {
        SetNextUpdate(SlowDataUpdated, SlowDataUpdatePeriod);

        Temperature = bme.readTemperature();
        // Serial.printf("Temperature: %3.1f °C \n", Temperature);

        // Calculate the dew point in Celsius
        gdewPoint = dewPoint(gTemperature, gHumidity);

        // Set N2K message
        SetN2kPGN130312(N2kMsg, 0, 0, gTempSource, CToKelvin(Temperature), N2kDoubleNA);

        // Send message
        NMEA2000.SendMsg(N2kMsg);

        // Set N2K message
        SetN2kPGN130312(N2kMsg, 0, 0, N2kts_DewPointTemperature, CToKelvin(gdewPoint), N2kDoubleNA);

        // Send message
        NMEA2000.SendMsg(N2kMsg);

        gTemperature = Temperature;
    }
}

void SendN2kHumidity(void) {
    static unsigned long SlowDataUpdated = InitNextUpdate(SlowDataUpdatePeriod, HumiditySendOffset);
    tN2kMsg N2kMsg;
    double Humidity;

    if (IsTimeToUpdate(SlowDataUpdated)) {
        SetNextUpdate(SlowDataUpdated, SlowDataUpdatePeriod);

        Humidity = bme.readHumidity();
        // Serial.printf("Humidity: %3.1f %% \n", Humidity);

        // Set N2K message
        SetN2kPGN130313(N2kMsg, 0, 0, N2khs_InsideHumidity, Humidity, N2kDoubleNA);

        // Send message
        NMEA2000.SendMsg(N2kMsg);

        gHumidity = Humidity;
    }
}

void SendN2kPressure(void) {
    static unsigned long SlowDataUpdated = InitNextUpdate(SlowDataUpdatePeriod, PressureSendOffset);
    tN2kMsg N2kMsg;
    double Pressure;

    if (IsTimeToUpdate(SlowDataUpdated)) {
        SetNextUpdate(SlowDataUpdated, SlowDataUpdatePeriod);

        Pressure = bme.readPressure() / 100;  // Read and convert to mBar 
        // Serial.printf("Pressure: %3.1f mBar \n", Pressure);

        // Set N2K message
        SetN2kPGN130314(N2kMsg, 0, 0, N2kps_Atmospheric, mBarToPascal(Pressure));

        // Send message
        NMEA2000.SendMsg(N2kMsg);

        gPressure = Pressure;
    }
}

void loop() {
    SendN2kTemperature();
    SendN2kHumidity();
    SendN2kPressure();

    NMEA2000.ParseMessages();


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