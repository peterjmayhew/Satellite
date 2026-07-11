#pragma once

// WiFi uplink: periodically POSTs a JSON telemetry packet to a WordPress REST
// endpoint. All configuration lives in secrets.h. If WiFi is not configured
// (empty SSID) the uplink disables itself and the device logs locally only.

void wifiUplinkInit();
void wifiUplinkLoop();
bool wifiUplinkIsConnected();
