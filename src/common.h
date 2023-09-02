// common.h

#pragma once

#ifndef _COMMON_h
#define _COMMON_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include "N2kMsg.h"
#include "N2kTypes.h"


extern uint8_t gN2KSource;
extern tN2kTempSource gTempSource;

extern double gTemperature;
extern double gHumidity;
extern double gPressure;

extern char Version[];

extern bool gParamsChanged;




#endif