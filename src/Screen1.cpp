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

// true only when the screen was just entered (see redrawScreenNow in main.cpp).
// When false we update the value fields in place instead of clearing the screen,
// so the 5 s refresh no longer blanks-and-repaints the whole panel (no flicker).
extern bool g_fullRedraw;

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

// Draw a value into a fixed-width field using opaque-background text. The trailing
// pad spaces (also drawn on the black background) overwrite any longer previous
// value, so nothing needs to be cleared first — no flash, no leftover pixels.
static void drawField(int x, int y, const String& value) {
  String v = value;
  const int FIELD = 18;                 // chars; 18*12px fits within the 320px width
  if (v.length() > FIELD) v = v.substring(0, FIELD);
  while (v.length() < FIELD) v += ' ';
  tft.setCursor(x, y);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print(v);
}

void updateDisplay1() {
  static bool lastFix = false;
  static bool drawn = false;
  // Repaint the static layout on screen entry, on the fix<->no-fix transition
  // (the two layouts differ), or the very first time.
  bool full = g_fullRedraw || (gpsFix != lastFix) || !drawn;
  lastFix = gpsFix;
  drawn = true;

  tft.setRotation(1);

  if (!gpsFix) {
    if (full) {
      tft.fillScreen(TFT_BLACK);
      tft.setTextSize(2);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setCursor(10, 20);
      tft.print("No GPS Fix!");
      tft.setTextSize(1);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.setCursor(10, 60);
      tft.print("Sats in view: ");
    }
    // Update just the count (padded so 2->1 digit leaves no ghost).
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    String s = String(satellitesSCRN1);
    while (s.length() < 3) s += ' ';
    tft.setCursor(10 + 14 * 6, 60);   // just after the "Sats in view: " label
    tft.print(s);
    return;
  }

  const int step = 26;
  const int valX = 82;                 // value column, past the widest (6-char) label
  static const char* const labels[9] = {
    "Lat:", "Lon:", "Alt:", "Sats:", "Spd:", "Hdg:", "HDOP:", "Time:", "Date:"
  };

  tft.setTextSize(2);

  if (full) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    for (int i = 0; i < 9; i++) {
      tft.setCursor(10, 8 + i * step);
      tft.print(labels[i]);
    }
  }

  const String vals[9] = {
    String(latitude, 6),
    String(longitude, 6),
    String(altitudeM, 1) + " m",
    String(satellitesSCRN1),
    String(speedKmph, 1) + " km/h",
    String(heading, 0) + " deg",
    String(hdop, 1) + " (" + accuracyFromHdop(hdop) + ")",
    timeUTC + " UTC",
    dateUTC
  };
  for (int i = 0; i < 9; i++) {
    drawField(valX, 8 + i * step, vals[i]);
  }
}
