#pragma once
#include <Arduino.h>

// Uses your existing globals (defined elsewhere in your project)
extern float latitude, longitude;
extern bool gpsFix;
extern String timeUTC, dateUTC;
extern float heading, speedKmph;
extern int satellitesSCRN1;

// Initialise NMEA parser state (does not reconfigure module)
void gpsParserInit(HardwareSerial &gps);

// Feed serial bytes and parse complete NMEA sentences
void gpsParserProcess(HardwareSerial &gps);
