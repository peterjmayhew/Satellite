#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;
extern float speedKmph;
extern bool gpsFix;

static float tripDistanceKm = 0.0f;
static float maxSpeedKmph = 0.0f;
static uint32_t movingMs = 0;
static uint32_t fixMs = 0;
static uint32_t lastMs = 0;

// Integrate distance/time. Called every loop iteration from main() so the trip
// accumulates continuously regardless of which screen is showing. (Previously
// this only ran while its screen was drawn, so returning to it added one huge
// dt and spiked the distance.)
void tripSample() {
  uint32_t now = millis();
  if (lastMs == 0) { lastMs = now; return; }

  uint32_t dt = now - lastMs;
  lastMs = now;

  if (gpsFix) fixMs += dt;

  if (gpsFix && speedKmph > 0.5f) {
    movingMs += dt;
    tripDistanceKm += (speedKmph * (dt / 3600000.0f)); // km/h * hours
    if (speedKmph > maxSpeedKmph) maxSpeedKmph = speedKmph;
  }
}

void updateDisplay6_Trip() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 10);
  tft.print("Trip Stats");

  auto label = [&](int y, const char* text) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(10, y);
    tft.print(text);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(120, y);
  };

  label(50, "Dist:");
  tft.print(tripDistanceKm, 2); tft.print(" km");

  label(80, "Max:");
  tft.print(maxSpeedKmph, 1); tft.print(" km/h");

  float avg = 0.0f;
  if (movingMs > 0) avg = tripDistanceKm / (movingMs / 3600000.0f);
  label(110, "Avg:");
  tft.print(avg, 1); tft.print(" km/h");

  label(150, "Fix:");
  tft.print(gpsFix ? "YES" : "NO");

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 220);
  tft.print("Fix time: "); tft.print(fixMs / 1000);
  tft.print(" s  Moving: "); tft.print(movingMs / 1000); tft.print(" s");
}
