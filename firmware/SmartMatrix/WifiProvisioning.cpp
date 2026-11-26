#include "WifiProvisioning.h"

#include <EEPROM.h>
#include "WifiDiagnostics.h"

namespace wifi_setup {
namespace {

const IPAddress SetupIp(192, 168, 4, 1);
const IPAddress NoIp(0, 0, 0, 0);
constexpr char SetupPassword[] = "SmartMatrix";
constexpr uint32_t SavedWindowMs = 30000;
constexpr uint32_t ConnectTimeoutMs = 30000;
constexpr uint32_t ConnectedWindowMs = 20000;
constexpr uint32_t RecordMagic = 0x31574D53;  // SMW1, first credential record format.

struct Record {
  uint32_t magic;
  wifi_http::Credentials credentials;
  uint16_t reserved;
  uint32_t checksum;
};

Phase phase = Phase::Setup;
uint32_t phaseStarted = 0;
bool setupStarted = false;
bool saveFailed = false;
bool pendingNeedsSave = false;
char token[wifi_http::TokenBytes + 1];
wifi_http::Credentials pending;
const __FlashStringHelper* failure = nullptr;
IPAddress lastIp;

uint32_t Checksum(const wifi_http::Credentials& credentials) {
  uint32_t hash = 2166136261u;
  for (unsigned char c : credentials.ssid) {
    hash = (hash ^ c) * 16777619u;
  }
  for (unsigned char c : credentials.password) {
    hash = (hash ^ c) * 16777619u;
  }
  return hash;
}

bool LoadCredentials(wifi_http::Credentials& credentials) {
  Record record{};
  EEPROM.get(0, record);
  if (record.magic != RecordMagic || record.checksum != Checksum(record.credentials) ||
      !wifi_http::ValidCredentials(record.credentials)) {
    return false;
  }
  credentials = record.credentials;
  return true;
}

bool SaveCredentials() {
  Record record{};
  record.magic = RecordMagic;
  record.credentials = pending;
  record.checksum = Checksum(pending);
  EEPROM.put(0, record);
  return EEPROM.commit();
}

void SetPhase(Phase next) {
  phase = next;
  phaseStarted = millis();
}

void Fail(const __FlashStringHelper* message) {
  failure = message;
  WiFi.disconnect();  // Clear the attempted credentials from SDK RAM, not EEPROM.
  WiFi.mode(setupStarted ? WIFI_AP : WIFI_OFF);
  pending = {};
  pendingNeedsSave = false;
  SetPhase(Phase::Failed);
}

void StartConnection() {
  if (!WiFi.mode(setupStarted ? WIFI_AP_STA : WIFI_STA)) {
    Fail(F("Could not start Wi-Fi station mode. Try again."));
    return;
  }
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);  // Don't mistake an SDK reconnect for success with the new settings.
  WiFi.begin(pending.ssid, pending.password);
  Serial.println(F("Connecting to Wi-Fi (DHCP)..."));
  SetPhase(Phase::Connecting);
}

}  // namespace

void Begin() {
  char name[sizeof("SmartMatrix-XXXX")];
  snprintf(name, sizeof(name), "SmartMatrix-%04X", static_cast<unsigned int>(ESP.getChipId() & 0xffff));
  WiFi.persistent(false);  // In particular, never save an unverified Wi-Fi.begin() in the SDK.
  BeginWifiDiagnostics();
  setupStarted = WiFi.mode(WIFI_AP) &&
                 WiFi.softAPConfig(SetupIp, SetupIp, IPAddress(255, 255, 255, 0)) &&
                 WiFi.softAP(name, SetupPassword);
  if (setupStarted) {
    Serial.print(F("Wi-Fi setup: "));
    Serial.print(name);
    Serial.println(F(" (AP password: SmartMatrix), http://192.168.4.1/wifi"));
    for (std::size_t i = 0; i < wifi_http::TokenBytes; i += 8) {
      snprintf(token + i, sizeof(token) - i, "%08lX", static_cast<unsigned long>(ESP.random()));
    }
  } else {
    WiFi.mode(WIFI_OFF);
    Serial.println(F("Could not start the Wi-Fi setup access point."));
  }
  EEPROM.begin(sizeof(Record));
  if (LoadCredentials(pending)) {
    SetPhase(Phase::SavedWindow);
  }
}

void Poll() {
  const IPAddress ip = StationIp();
  const auto status = WiFi.status();
  if (phase == Phase::Connecting || phase == Phase::ConnectedWindow || phase == Phase::Online) {
    PollWifiDiagnostics(status, ip != NoIp);
  }
  if (ip != lastIp) {
    lastIp = ip;
    if (ip == NoIp) {
      Serial.println(F("Wi-Fi: IPv4 address lost."));
    } else {
      Serial.print(F("NodeMCU IPv4: "));
      Serial.println(ip);
    }
  }

  const auto elapsed = millis() - phaseStarted;
  switch (phase) {
    case Phase::SavedWindow:
      if (!setupStarted || elapsed >= SavedWindowMs) {
        StartConnection();
      }
      break;
    case Phase::Queued:
      StartConnection();
      break;
    case Phase::Connecting:
      if (ip != NoIp) {
        saveFailed = pendingNeedsSave && !SaveCredentials();
        if (saveFailed) {
          Serial.println(F("Could not save Wi-Fi settings to flash."));
        }
        pending = {};
        pendingNeedsSave = false;
        SetPhase(Phase::ConnectedWindow);
      } else if (elapsed >= ConnectTimeoutMs) {
        if (status == WL_WRONG_PASSWORD) {
          Fail(F("Authentication failed. Check the Wi-Fi password and try again."));
        } else if (status == WL_NO_SSID_AVAIL) {
          Fail(F("Network not found. Check the 2.4 GHz network name and try again."));
        } else if (WifiIsAssociated()) {
          Fail(F("No IPv4 address from DHCP. Check the router and try again."));
        } else {
          Fail(F("Connection timed out. Check the network name, password and DHCP, then try again."));
        }
      }
      break;
    case Phase::ConnectedWindow:
      if (ip == NoIp) {
        Fail(F("Wi-Fi disconnected during setup. Try again."));
      } else if ((!setupStarted || elapsed >= ConnectedWindowMs) && WiFi.mode(WIFI_STA)) {
        setupStarted = false;
        WiFi.setAutoReconnect(true);
        SetPhase(Phase::Online);
        Serial.println(F("Wi-Fi ready: GET /, GET /wifi and POST /frame are available on port 80."));
      }
      break;
    case Phase::Setup:
    case Phase::Online:
    case Phase::Failed:
      break;
  }
}

Phase CurrentPhase() {
  return phase;
}

const __FlashStringHelper* StatusMessage() {
  switch (phase) {
    case Phase::Setup: return F("Enter the settings for a 2.4 GHz Wi-Fi network.");
    case Phase::SavedWindow:
      return F("Saved settings will be tried shortly. Submit this form to use a different network.");
    case Phase::Queued:
    case Phase::Connecting: return F("Connecting and waiting for an IPv4 address...");
    case Phase::ConnectedWindow:
    case Phase::Online:
      if (StationIp() == NoIp) {
        return F("Disconnected. Trying to reconnect.");
      }
      return saveFailed ? F("Connected, but the settings could not be saved to flash.") : F("Connected.");
    case Phase::Failed: return failure;
  }
  return F("Unknown Wi-Fi status.");
}

IPAddress StationIp() {
  return (WiFi.getMode() & WIFI_STA) && WiFi.status() == WL_CONNECTED ? WiFi.localIP() : NoIp;
}

bool IsSetupAddress(IPAddress address) {
  return setupStarted && address == SetupIp && StationIp() != SetupIp;
}

bool CanConfigure() {
  // Accept secrets only with STA disabled, not just when the destination IP matches the AP.
  return setupStarted && WiFi.getMode() == WIFI_AP &&
         (phase == Phase::Setup || phase == Phase::SavedWindow || phase == Phase::Failed);
}

std::string_view FormToken() {
  return {token, wifi_http::TokenBytes};
}

bool QueueConnection(const wifi_http::Credentials& credentials) {
  if (!CanConfigure()) {
    return false;
  }
  pending = credentials;
  pendingNeedsSave = true;
  SetPhase(Phase::Queued);
  return true;
}

}  // namespace wifi_setup
