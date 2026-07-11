#pragma once
#include <Arduino.h>

// Sync ESP32 system time from GPS data
void syncTimeFromGPS(const String& dateUTC, const String& timeUTC);
// Get current time as formatted string (YYYY-MM-DD HH:MM:SS)
String getCurrentTimeString();

// Display current time on TFT at specified position
void displayTimeOnTFT(int x, int y, uint16_t color = 0xFFFF);

// Convert time_t to individual date/time components
// Returns components in a struct-like manner through parameters
void getDateTimeFromTimestamp(time_t timestamp, uint16_t& year, uint8_t& month, 
                               uint8_t& day, uint8_t& hour, uint8_t& minute);