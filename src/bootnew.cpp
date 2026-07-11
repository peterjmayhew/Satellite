#include <Arduino.h>
#include "display.h"
#include "config.h"
#include "sdcard.h"
#include "gps.h"

// Amazing boot screen with graphics
void bootScreenNew() {
  // Clear screen
  tft.fillScreen(ILI9341_BLACK);
  tft.setRotation(1);

  // Draw decorative top border
  tft.fillRect(0, 0, 320, 3, ILI9341_CYAN);
  tft.fillRect(0, 0, 3, 240, ILI9341_CYAN);
  tft.fillRect(317, 0, 3, 240, ILI9341_CYAN);
  tft.fillRect(0, 237, 320, 3, ILI9341_CYAN);

  // Draw centered title with gradient effect using multiple text sizes
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setCursor(50, 20);
  tft.println("SATELLITE");

  tft.setTextSize(2);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft.setCursor(70, 55);
  tft.println("GPS Logger");
  tft.setTextSize(1);
  tft.setCursor(70, 75); // this needs to change
  tft.println("By Peter J Mayhew");
  
  // Draw decorative lines
  tft.drawLine(40, 85, 280, 85, ILI9341_CYAN);
  tft.drawLine(40, 87, 280, 87, ILI9341_GREEN);

  // Draw satellite indicator circles
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(30, 95);
  tft.println("* Initializing Systems *");

  // Draw animated loading bar background
  tft.drawRect(30, 125, 260, 20, ILI9341_CYAN);
  
  // Animate loading bar with segments
  int barWidth = 260 - 4;
  int segmentWidth = barWidth / 5;
  
  for (int i = 0; i < 5; i++) {
    tft.fillRect(32 + (i * segmentWidth), 127, segmentWidth - 2, 16, ILI9341_GREEN);
    delay(200);
  }

  // Draw system status messages with color coding
  tft.setTextSize(1);
  
  // GPS Status - Check actual status
  tft.setCursor(40, 155);
  if (gpsIsReady()) {
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.println("[ OK ] GPS Serial");
  } else {
    tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
    tft.println("[WAIT] GPS Serial");
  }
  
  // TFT Status
  tft.setCursor(40, 170);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft.println("[ OK ] TFT Display");
  
  // SD Status - Check actual status
  tft.setCursor(40, 185);
  if (sdIsReady()) {
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.println("[ OK ] SD Card");
  } else {
    tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft.println("[FAIL] SD Card");
  }

   // GPS Mode Check actual status
  tft.setCursor(40, 195);
  if (!USE_UBX) {
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.println("[ OK ] NMEA Parser");
  } else {
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.println("[ OK ] UBX Parser");
  }

  // Draw decorative corner elements
  tft.drawCircle(25, 25, 8, ILI9341_YELLOW);
  tft.drawCircle(295, 25, 8, ILI9341_YELLOW);
  tft.drawCircle(25, 215, 8, ILI9341_YELLOW);
  tft.drawCircle(295, 215, 8, ILI9341_YELLOW);

  // Version info in footer
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  tft.setCursor(110, 210);
  tft.print("v");
  tft.print(VERSION);
  
  // Countdown text
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(80, 225);
  tft.println("Starting in 3...");
  delay(500);
  
  // Draw wavy line at bottom for cool effect
  for (int x = 0; x < 320; x += 10) {
    int y = 237 + (int)(2 * sin(x * 0.05));
    tft.drawPixel(x, y, ILI9341_GREEN);
    tft.drawPixel(x + 1, y, ILI9341_CYAN);
  }
}
