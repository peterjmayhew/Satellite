#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "timesync.h"

// Live GPS data (defined in main.cpp)
extern float latitude, longitude, speedKmph, heading;
extern float altitudeM, hdop;
extern int satellitesSCRN1;
extern bool gpsFix;
extern String timeUTC, dateUTC;

void updateDisplay1();

// Simple horizontal-accuracy hint from HDOP (rule of thumb only)
static const char* accuracyFromHdop(float h) {
  if (h <= 0)   return "--";
  if (h < 1.0f) return "Ideal";
  if (h < 2.0f) return "Excellent";
  if (h < 5.0f) return "Good";
  if (h < 10.0f) return "Moderate";
  return "Poor";
}

void updateDisplay1() {
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);

  if (!gpsFix) {
    tft.setCursor(10, 20);
    tft.setTextColor(TFT_RED);
    tft.println("No GPS Fix!");
    tft.setTextSize(1);
    tft.setCursor(10, 60);
    tft.setTextColor(TFT_YELLOW);
    tft.print("Sats in view: ");
    tft.println(satellitesSCRN1);
    return;
  }

  int y = 8;
  const int step = 26;

  auto row = [&](const char* label, const String& value) {
    tft.setCursor(10, y);
    tft.setTextColor(TFT_YELLOW);
    tft.print(label);
    tft.setTextColor(TFT_WHITE);
    tft.println(value);
    y += step;
  };

  row("Lat: ",  String(latitude, 6));
  row("Lon: ",  String(longitude, 6));
  row("Alt: ",  String(altitudeM, 1) + " m");
  row("Sats: ", String(satellitesSCRN1));
  row("Spd: ",  String(speedKmph, 1) + " km/h");
  row("Hdg: ",  String(heading, 0) + " deg");
  row("HDOP: ", String(hdop, 1) + " (" + accuracyFromHdop(hdop) + ")");
  row("Time: ", timeUTC + " UTC");
  row("Date: ", dateUTC);
}
