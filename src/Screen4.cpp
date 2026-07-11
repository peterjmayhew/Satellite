#include <Arduino.h>
#include "display.h"

// Shared globals (defined in main.cpp / config.cpp)
extern float speedHistory[30];
extern float currentSpeed;
extern float currentHeading;
extern unsigned long lastUpdate;
extern const unsigned long updateInterval;
extern float speedKmph, heading;
extern bool gpsFix;

// Full-scale for the speedometer + history graph (km/h)
static const float SPEED_FULL_SCALE = 160.0f;

void updateDisplay4();
void speedSample();
static void drawSpeedometer();
static void drawCompass();
static void drawSpeedGraph();

// Called every loop from main(); self-gates on updateInterval. Samples the real
// GPS speed/heading into the rolling history so the graph fills even when this
// screen isn't the one on display.
void speedSample() {
  if (millis() - lastUpdate < updateInterval) return;
  lastUpdate = millis();

  currentSpeed = gpsFix ? speedKmph : 0.0f;
  currentHeading = heading;

  for (int i = 0; i < 29; i++) speedHistory[i] = speedHistory[i + 1];
  speedHistory[29] = currentSpeed;
}

void updateDisplay4() {
  tft.fillScreen(TFT_BLACK);
  drawSpeedometer();
  drawCompass();
  drawSpeedGraph();
}

static void drawSpeedometer() {
  tft.fillRect(0, 0, 160, 120, TFT_BLACK);
  tft.setCursor(20, 5);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println("Speed");

  int cx = 80, cy = 100, r = 40;
  float clamped = currentSpeed;
  if (clamped > SPEED_FULL_SCALE) clamped = SPEED_FULL_SCALE;
  float angle = map((long)(clamped * 10), 0, (long)(SPEED_FULL_SCALE * 10), -150, -30);
  float rad = angle * DEG_TO_RAD;
  int x = cx + cos(rad) * r;
  int y = cy + sin(rad) * r;

  tft.drawCircle(cx, cy, r, TFT_WHITE);
  tft.drawLine(cx, cy, x, y, TFT_RED);

  tft.setCursor(20, 108);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.print(currentSpeed, 1);
  tft.println(" km/h");
}

static void drawCompass() {
  tft.fillRect(160, 0, 160, 120, TFT_BLACK);
  int cx = 240, cy = 60, r = 40;
  // 0 deg = North (up); rotate so heading points correctly
  float rad = (currentHeading - 90.0f) * DEG_TO_RAD;
  int x = cx + cos(rad) * r;
  int y = cy + sin(rad) * r;

  tft.drawCircle(cx, cy, r, TFT_WHITE);
  tft.drawLine(cx, cy, x, y, TFT_GREEN);

  tft.setCursor(180, 5);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println("Heading");

  tft.setCursor(190, 108);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.print(currentHeading, 0);
  tft.println(" deg");
}

static void drawSpeedGraph() {
  tft.fillRect(0, 120, 320, 120, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 122);
  tft.println("Speed history (km/h)");

  for (int i = 0; i < 30; i++) {
    int barHeight = map((long)(speedHistory[i] * 10), 0,
                        (long)(SPEED_FULL_SCALE * 10), 0, 100);
    if (barHeight < 0) barHeight = 0;
    if (barHeight > 100) barHeight = 100;
    int x = 10 + i * 10;
    tft.fillRect(x, 220 - barHeight, 6, barHeight, TFT_BLUE);
  }
}
