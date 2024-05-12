// common.h

#pragma once

#ifndef _COMMON_h
#define _COMMON_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

extern bool debugMode;

#define DEBUG_PRINT(x) if (debugMode) Serial.print(x) 
#define DEBUG_PRINTLN(x) if (debugMode) Serial.println(x)
#define DEBUG_PRINTF(...) if (debugMode) Serial.printf(__VA_ARGS__)

#include "N2kMsg.h"
#include "N2kTypes.h"

#define DeviceTemperature 0
#define DevicePressure 1
#define DeviceHumidity 2

extern uint8_t gN2KInstance;
extern uint8_t gN2KSID;
extern uint8_t gN2KSource[];

extern tN2kTempSource gTempSource;
extern tN2kHumiditySource gHumiditySource;

extern double gTemperature;
extern double gHumidity;
extern double gPressure;
extern double gdewPoint;
extern double gheatIndex;

extern char Version[];

extern bool gParamsChanged;
extern bool gSaveParams;

#endif