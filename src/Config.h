// Config.h

#ifndef _CONFIG_h
#define _CONFIG_h

// ============================================================================
// BOARD CONFIGURATION
// ============================================================================
// Select your target board by uncommenting ONE of the following definitions:
// - Uncomment the board you are using
// - Leave all others commented out
// ============================================================================

#define BOARD_NMEA_SENSOR_BOARD          // NMEA Sensor Board 
// #define BOARD_WEMOS_D1_MINI_ESP32    // Wemos D1 Mini ESP32
// #define BOARD_NODE32S                // NodeMCU-32S
// #define BOARD_ESP32_DEVKIT           // ESP32 DevKit V1
// #define BOARD_CUSTOM                 // Custom board (define pins below)




#if defined(BOARD_WEMOS_D1_MINI_ESP32)
    // ----------------------------------------------------
    // Wemos D1 Mini ESP32 Pin Configuration
    // ----------------------------------------------------
    #define ESP32_CAN_TX_PIN GPIO_NUM_16    // CAN Bus TX Pin
    #define ESP32_CAN_RX_PIN GPIO_NUM_17    // CAN Bus RX Pin

    #define BOARD_NAME "Wemos D1 Mini ESP32"
    #define BOARD_INFO "Standard Wemos D1 Mini ESP32 configuration"

    #define DISABLE_BROWNOUT_DETECTOR

#elif defined(BOARD_NODE32S)
    // ----------------------------------------------------
    // NodeMCU-32S Pin Configuration
    // ----------------------------------------------------
    #define ESP32_CAN_TX_PIN GPIO_NUM_5     // CAN Bus TX Pin
    #define ESP32_CAN_RX_PIN GPIO_NUM_4     // CAN Bus RX Pin

    #define BOARD_NAME "NodeMCU-32S"
    #define BOARD_INFO "NodeMCU-32S standard configuration"

#elif defined(BOARD_ESP32_DEVKIT)
    // ----------------------------------------------------
    // ESP32 DevKit V1 Pin Configuration
    // ----------------------------------------------------
    #define ESP32_CAN_TX_PIN GPIO_NUM_5     // CAN Bus TX Pin
    #define ESP32_CAN_RX_PIN GPIO_NUM_4     // CAN Bus RX Pin

    #define BOARD_NAME "ESP32 DevKit V1"
    #define BOARD_INFO "ESP32 DevKit V1 standard configuration"

#elif defined(BOARD_NMEA_SENSOR_BOARD)
    // ----------------------------------------------------
    // NMEA Sensor Board Pin Configuration
    // ----------------------------------------------------
    #define ESP32_CAN_TX_PIN GPIO_NUM_16     // CAN Bus TX Pin
    #define ESP32_CAN_RX_PIN GPIO_NUM_17     // CAN Bus RX Pin

    #define BOARD_NAME "NMEA Sensor Board"
    #define BOARD_INFO "Custom NMEA Sensor Board configuration"

#elif defined(BOARD_CUSTOM)
    // ----------------------------------------------------
    // Custom Board Pin Configuration
    // ----------------------------------------------------
    // Define your custom pin assignments here
    #define ESP32_CAN_TX_PIN GPIO_NUM_XX    // CAN Bus TX Pin
    #define ESP32_CAN_RX_PIN GPIO_NUM_XX    // CAN Bus RX Pin

    #define BOARD_NAME "Custom Board"
    #define BOARD_INFO "User-defined custom board configuration"

#else
    #error "No board selected! Please uncomment one BOARD_xxx definition in Config.h"
#endif

// ============================================================================
// BME280 I2C ADDRESS
// ============================================================================
// The BME280 sensor can have two possible I2C addresses depending on the SDO pin:
// - 0x76 (SDO pin connected to GND) - BME280_ADDRESS_ALTERNATE
// - 0x77 (SDO pin connected to VCC) - BME280_ADDRESS
// ============================================================================
#define BME280_I2C_ADDRESS BME280_ADDRESS_ALTERNATE  // Default: 0x76

#endif // _CONFIG_h