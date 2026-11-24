#pragma once

#include <ESP8266WiFi.h>

void BeginWifiDiagnostics();
void PollWifiDiagnostics(wl_status_t status, bool hasIp);
