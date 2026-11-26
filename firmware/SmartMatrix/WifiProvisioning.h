#pragma once

#include <ESP8266WiFi.h>
#include "WifiHttpRequest.h"

namespace wifi_setup {

enum class Phase { Setup, SavedWindow, Queued, Connecting, ConnectedWindow, Online, Failed };

void Begin();
void Poll();
Phase CurrentPhase();
const __FlashStringHelper* StatusMessage();
IPAddress StationIp();
bool IsSetupAddress(IPAddress address);
bool CanConfigure();
std::string_view FormToken();
bool QueueConnection(const wifi_http::Credentials& credentials);

}  // namespace wifi_setup
