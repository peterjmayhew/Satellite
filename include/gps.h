#pragma once
#include <Arduino.h>
void gpsParserInit(HardwareSerial &gps);
void gpsParserProcess(HardwareSerial &gps);

void gpsUpdateLine(const String& nmea);
const String& gpsGetLastLine();

// Check if GPS has received any valid data
bool gpsIsReady();

// Optional later:
bool gpsHasFix();
float gpsLat();
float gpsLon();

//logging mode
enum GpsLogMode {
  GPS_LOG_NONE = 0,
  GPS_LOG_RAW_NMEA,
  GPS_LOG_CSV,
  GPS_LOG_BOTH
};

extern GpsLogMode gpsLogMode;