#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "sdcard.h"
#include "timesync.h"   // getDateTimeFromTimestamp(), displayTimeOnTFT()

static SdFileInfo fileList[50];

static String formatFileSize(uint32_t bytes) {
  if (bytes < 1024) return String(bytes) + "B";
  if (bytes < 1024UL * 1024UL) return String(bytes / 1024.0, 1) + "KB";
  return String(bytes / (1024.0 * 1024.0), 1) + "MB";
}

void updateDisplay9_SDBrowser() {
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft.setCursor(10, 10);
  tft.println("SD Card Browser");

  displayTimeOnTFT(10, 25, ILI9341_CYAN);

  if (!sdIsReady()) {
    tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft.setCursor(10, 40);
    tft.println("SD not ready");
    return;
  }

  int fileCount = sdListRoot(fileList, 50);

  if (fileCount == 0) {
    tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
    tft.setCursor(10, 40);
    tft.println("No files found");
    return;
  }

  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setCursor(10, 40);
  tft.print("Files: ");
  tft.println(fileCount);

  const int startY = 60;
  const int lineHeight = 18;
  const int displayCount = (fileCount > 10) ? 10 : fileCount;

  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);

  for (int i = 0; i < displayCount; i++) {
    int yPos = startY + (i * lineHeight);

    String name = fileList[i].name;
    if (name.length() > 18) name = name.substring(0, 15) + "...";
    tft.setCursor(10, yPos);
    tft.print(name);

    tft.setCursor(100, yPos);
    tft.print(formatFileSize(fileList[i].size));

    if (fileList[i].mtime > 0) {
      uint16_t year; uint8_t month, day, hour, minute;
      getDateTimeFromTimestamp((time_t)fileList[i].mtime, year, month, day, hour, minute);
      if (year > 1970) {
        char dateStr[12];
        snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", year, month, day);
        tft.setCursor(155, yPos);
        tft.print(dateStr);

        char timeStr[6];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, minute);
        tft.setCursor(265, yPos);
        tft.print(timeStr);
      }
    }
  }

  if (fileCount > 10) {
    tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
    tft.setCursor(10, 224);
    tft.print("... ");
    tft.print(fileCount - 10);
    tft.print(" more files");
  }
}
