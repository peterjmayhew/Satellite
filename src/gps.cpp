#include "gps.h"

static String lastNmea = "";

void gpsUpdateLine(const String& nmea) {
  lastNmea = nmea;
}

const String& gpsGetLastLine() {
  return lastNmea;
}
// Check if GPS has received any valid NMEA data
bool gpsIsReady() {
  return lastNmea.length() > 0;
}