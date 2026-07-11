#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "sdcard.h"
#include "gps.h"

// SD readback test screen: appends the latest position sentence to a file and
// reads the last line back, exercising the SD write/read path end-to-end.
void updateDisplay7_SDReadback() {
  const int x = 10;
  int y = 8;
  const int step = 16;

  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft.setCursor(x, y);
  tft.println("SD Readback");
  y += step;

  if (!sdIsReady()) {
    tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft.setCursor(x, y);
    tft.println("SD not ready");
    return;
  }

  const char* path = "/gps_log2.txt";

  if (!sdExists(path)) {
    sdWriteText(path, "New GPS file created by Screen7", false);
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.setCursor(x, y);
    tft.println("Created new log file");
    y += step;
  }

  const String& line = gpsGetLastLine();
  if (line.length() == 0) {
    tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft.setTextSize(2);
    tft.setCursor(x, y + 8);
    tft.println("No NMEA yet");
  } else {
    sdWriteText(path, line.c_str(), true);
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.setCursor(x, y);
    tft.println("Last NMEA:");
    y += step;
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.setCursor(x, y);
    tft.println(line);
    y += step;
  }

  y += step;
  String last;
  if (sdReadLastNonEmptyLine(path, last)) {
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.setCursor(x, y);
    tft.println("Last line in file:");
    y += step;
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.setCursor(x, y);
    tft.println(last);
  } else {
    tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft.setCursor(x, y);
    tft.println("No data");
  }
}
