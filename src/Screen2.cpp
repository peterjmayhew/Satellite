#include <Arduino.h>
#include "display.h"
#include "gnss_extra.h"

// Live GPS data (defined in main.cpp)
extern bool gpsFix;
extern int satellitesSCRN1;

void updateDisplay2();

// Count satellites currently in view per constellation, plus how many have a
// usable signal (SNR > 0). Data comes from the shared sky list populated by the
// GSV / NAV-SAT parser.
struct ConstStat { const char* name; char talker; int count; int strong; };

void updateDisplay2() {
  ConstStat c[] = {
    { "GPS",     'P', 0, 0 },
    { "GLONASS", 'L', 0, 0 },
    { "Galileo", 'A', 0, 0 },
    { "BeiDou",  'B', 0, 0 },
    { "QZSS",    'Q', 0, 0 },
  };
  const int N = sizeof(c) / sizeof(c[0]);

  int totalInView = 0;
  for (int i = 0; i < skySatCount; i++) {
    for (int k = 0; k < N; k++) {
      if (skySats[i].talker == c[k].talker) {
        c[k].count++;
        if (skySats[i].snr > 0) c[k].strong++;
        totalInView++;
        break;
      }
    }
  }

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setCursor(10, 8);
  tft.println("Satellites in View");

  tft.setTextSize(1);
  tft.setTextColor(ILI9341_DARKGREY, ILI9341_BLACK);
  tft.setCursor(10, 30);
  tft.printf("Total in view: %d   Used in fix: %d", totalInView, satellitesSCRN1);

  int y = 52;
  const int step = 30;
  tft.setTextSize(2);
  for (int k = 0; k < N; k++) {
    tft.setCursor(10, y);
    tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
    tft.print(c[k].name);
    tft.print(":");

    tft.setCursor(170, y);
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.print(c[k].count);

    // "strong" (has signal) shown in green next to the count
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.setTextSize(1);
    tft.setCursor(210, y + 4);
    tft.printf("(%d w/signal)", c[k].strong);
    tft.setTextSize(2);

    y += step;
  }

  if (!gpsFix) {
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft.setCursor(10, 220);
    tft.print("No fix yet - acquiring...");
  }
}
