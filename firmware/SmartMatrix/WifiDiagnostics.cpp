#include "WifiDiagnostics.h"

namespace {

constexpr uint32_t ReportIntervalMs = 5000;

// Keep the handles alive or the ESP8266 core stops delivering these events.
WiFiEventHandler connectedHandler;
WiFiEventHandler disconnectedHandler;
WiFiEventHandler dhcpTimeoutHandler;

bool associated = false;
bool connectedPending = false;
bool disconnectedPending = false;
bool dhcpTimeoutPending = false;
uint8_t channel = 0;
WiFiDisconnectReason reason = WIFI_DISCONNECT_REASON_UNSPECIFIED;
uint32_t lastReport = 0;

const __FlashStringHelper* DisconnectHint(WiFiDisconnectReason code) {
  switch (code) {
    case WIFI_DISCONNECT_REASON_NO_AP_FOUND: return F("access point not found");
    case WIFI_DISCONNECT_REASON_AUTH_FAIL: return F("authentication failed");
    case WIFI_DISCONNECT_REASON_ASSOC_FAIL: return F("association failed");
    case WIFI_DISCONNECT_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_DISCONNECT_REASON_HANDSHAKE_TIMEOUT: return F("security handshake timed out");
    case WIFI_DISCONNECT_REASON_BEACON_TIMEOUT: return F("access point signal lost");
    default: return F("see ESP8266 disconnect reason code");
  }
}

const __FlashStringHelper* StatusLabel(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return F("idle");
    case WL_NO_SSID_AVAIL: return F("access point not found");
    case WL_CONNECTED: return F("connected");
    case WL_CONNECT_FAILED: return F("connection failed");
    case WL_WRONG_PASSWORD: return F("wrong password");
    case WL_DISCONNECTED: return F("connecting or disconnected");
    default: return F("other");
  }
}

}  // namespace

void BeginWifiDiagnostics() {
  // Defer Serial output to loop() rather than writing from SDK event callbacks.
  connectedHandler = WiFi.onStationModeConnected([](const WiFiEventStationModeConnected& event) {
    associated = true;
    channel = event.channel;
    connectedPending = true;
  });
  disconnectedHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event) {
    associated = false;
    reason = event.reason;
    disconnectedPending = true;
  });
  dhcpTimeoutHandler = WiFi.onStationModeDHCPTimeout([] {
    dhcpTimeoutPending = true;
  });
  lastReport = millis();
}

bool WifiIsAssociated() {
  return associated;
}

void PollWifiDiagnostics(wl_status_t status, bool hasIp) {
  if (connectedPending) {
    connectedPending = false;
    Serial.print(F("Wi-Fi: associated, channel "));
    Serial.print(channel);
    Serial.println(F("; waiting for DHCP."));
  }
  if (disconnectedPending) {
    disconnectedPending = false;
    const auto code = reason;
    Serial.print(F("Wi-Fi: disconnected, reason "));
    Serial.print(static_cast<unsigned>(code));
    Serial.print(F(" ("));
    Serial.print(DisconnectHint(code));
    Serial.println(F(")."));
  }
  if (dhcpTimeoutPending) {
    dhcpTimeoutPending = false;
    Serial.println(F("Wi-Fi: DHCP timed out; no IPv4 address received."));
  }

  const auto now = millis();
  if (hasIp) {
    lastReport = now;
  } else if (now - lastReport >= ReportIntervalMs) {
    lastReport = now;
    Serial.print(associated ? F("Wi-Fi: still waiting for DHCP. Status ") :
                             F("Wi-Fi: still waiting for association. Status "));
    Serial.print(static_cast<int>(status));
    Serial.print(F(" ("));
    Serial.print(StatusLabel(status));
    Serial.println(F(")."));
  }
}
