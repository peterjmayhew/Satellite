#include <Arduino.h>
#include <time.h>
#include "timesync.h"
#include "display.h"

// Sync ESP32 system time from GPS data
// dateUTC format: DD/MM/YYYY (from formatDate function)
// timeUTC format: HH:MM:SS (from formatTime function)
void syncTimeFromGPS(const String& dateUTC, const String& timeUTC) {
  if (dateUTC.length() < 10 || timeUTC.length() < 8) {
    Serial.println("Invalid date/time format");
    return;
  }

  // Parse date (DD/MM/YYYY format)
  int day = dateUTC.substring(0, 2).toInt();
  int month = dateUTC.substring(3, 5).toInt();
  int year = dateUTC.substring(6, 10).toInt();  // Extract full 4-digit year

  // Parse time (HH:MM:SS format)
  int hour = timeUTC.substring(0, 2).toInt();
  int minute = timeUTC.substring(3, 5).toInt();
  int second = timeUTC.substring(6, 8).toInt();

  // Validate ranges
  if (month < 1 || month > 12 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59 || year < 2000 || year > 2100) {
    Serial.print("Invalid date/time values: ");
    Serial.print(dateUTC);
    Serial.print(" ");
    Serial.println(timeUTC);
    return;
  }

  // Create time structure
  struct tm timeinfo = {};
  timeinfo.tm_year = year - 1900;  // years since 1900
  timeinfo.tm_mon = month - 1;     // months since January (0-11)
  timeinfo.tm_mday = day;
  timeinfo.tm_hour = hour;
  timeinfo.tm_min = minute;
  timeinfo.tm_sec = second;

  // Convert to time_t and set system time
  time_t t = mktime(&timeinfo);
  struct timeval tv = {.tv_sec = t, .tv_usec = 0};
  settimeofday(&tv, NULL);

  Serial.print("System time synced to: ");
  Serial.print(year);
  Serial.print("-");
  if (month < 10) Serial.print("0");
  Serial.print(month);
  Serial.print("-");
  if (day < 10) Serial.print("0");
  Serial.print(day);
  Serial.print(" ");
  Serial.print(timeUTC);
  Serial.println(" UTC");
}

// Get current time as formatted string (YYYY-MM-DD HH:MM:SS)
String getCurrentTimeString() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  
  char buffer[25];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
           timeinfo->tm_year + 1900,
           timeinfo->tm_mon + 1,
           timeinfo->tm_mday,
           timeinfo->tm_hour,
           timeinfo->tm_min,
           timeinfo->tm_sec);
  
  return String(buffer);
}

// Display current time on TFT at specified position
void displayTimeOnTFT(int x, int y, uint16_t color) {
  String timeStr = getCurrentTimeString();
  
  tft.setCursor(x, y);
  tft.setTextColor(color, ILI9341_BLACK);
  tft.setTextSize(1);
  tft.println(timeStr);
}
// Convert time_t timestamp to individual date/time components
// This properly handles the time_t format returned by getLastWrite()
void getDateTimeFromTimestamp(time_t timestamp, uint16_t& year, uint8_t& month, 
                               uint8_t& day, uint8_t& hour, uint8_t& minute) {
  struct tm* timeinfo = localtime(&timestamp);
  
  year = timeinfo->tm_year + 1900;   // tm_year is years since 1900
  month = timeinfo->tm_mon + 1;      // tm_mon is 0-11, we want 1-12
  day = timeinfo->tm_mday;           // tm_mday is already 1-31
  hour = timeinfo->tm_hour;          // tm_hour is 0-23
  minute = timeinfo->tm_min;         // tm_min is 0-59
}