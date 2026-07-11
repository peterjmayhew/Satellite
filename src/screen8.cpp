#include <Arduino.h>
#include "display.h"   // provides global TFT_eSPI tft
#include "config.h"    // pin macros if needed elsewhere
#include "sdcard.h"    // sdIsReady(), sdForEachLine()

///////////////////////////////////////
// Description. 
// Shows the contens of the gps file
///////////////////////////////////////

// Simple context struct to track cursor position while printing lines
struct TftPrintCtx {
  int x;
  int y;
  int lineH;
  int maxY;
  uint8_t textSize;
};

// Callback: called once per line by sdForEachLine()
static void tftLineSink(const char* line, void* user) {
  auto* ctx = reinterpret_cast<TftPrintCtx*>(user);

  // Stop when we're at the bottom of the display
  if (ctx->y > ctx->maxY) return;

  tft.setCursor(ctx->x, ctx->y);
  tft.println(line);
  ctx->y += ctx->lineH;
}

// New screen: show full file contents (or first N lines) on TFT
void updateDisplay8_SDFileViewer() {
  const char* path = "/gps_log2.txt";  // change if you want

  // Clear + header
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("SD File Viewer");

  // SD ready?
  if (!sdIsReady()) {
    tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft.setCursor(10, 50);
    tft.println("SD not ready");
    return;
  }

  // Draw filename
  tft.setTextSize(1);
  tft.setCursor(10, 35);
  tft.println(path);

  // Setup printing context
  TftPrintCtx ctx;
  ctx.x = 0;
  ctx.y = 50;
  ctx.textSize = 1;

  // Text size 1 is ~8px tall on ILI9341 with default font
  ctx.lineH = 10;   // tweak for readability
  ctx.maxY  = 235;  // bottom limit for 240px tall screen

  // Print file line-by-line
  // maxLines = 200, maxLineLen = 200 (tweak as needed)
  bool ok = sdForEachLine(path, tftLineSink, &ctx, 200, 200);

  if (!ok) {
    tft.setTextColor(ILI9341_RED, ILI9341_BLACK);
    tft.setCursor(10, 60);
    tft.setTextSize(2);
    tft.println("Open/read failed");
  }
}
