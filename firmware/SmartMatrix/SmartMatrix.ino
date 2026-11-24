#include <ESP8266WiFi.h>
#include <FastLED.h>
#include "FrameHttpServer.h"
#include "WifiDiagnostics.h"
#include "secrets.h"

// Change these after the matrix is wired.
constexpr uint8_t DataPin = D5;  // GPIO14 on NodeMCU 1.0
constexpr uint8_t Brightness = 64;
constexpr auto FirstPixel = matrix::FirstCorner::TopLeft;
constexpr bool ShowStartupTest = false;

CRGB leds[matrix::PixelCount];
IPAddress lastIp;

void RenderFrame(const matrix::Rgb (&frame)[matrix::PixelCount]);

void RenderFrame(const matrix::Rgb (&frame)[matrix::PixelCount]) {
  for (uint16_t y = 0; y < matrix::Height; ++y) {
    for (uint16_t x = 0; x < matrix::Width; ++x) {
      const auto& pixel = frame[y * matrix::Width + x];
      leds[matrix::LedIndex(x, y, FirstPixel)] = CRGB(pixel.r, pixel.g, pixel.b);
    }
  }
  FastLED.show();
}

void DrawStartupTest() {
  matrix::Rgb frame[matrix::PixelCount] = {};
  frame[0] = {255, 0, 0};                                          // top left
  frame[matrix::Width - 1] = {0, 255, 0};                           // top right
  frame[(matrix::Height - 1) * matrix::Width] = {0, 0, 255};        // bottom left
  frame[matrix::PixelCount - 1] = {255, 255, 255};                  // bottom right
  RenderFrame(frame);
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DataPin, GRB>(leds, matrix::PixelCount);
  FastLED.setBrightness(Brightness);
  FastLED.clear(true);

  if (ShowStartupTest) {
    DrawStartupTest();
  }

  BeginWifiDiagnostics();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  Serial.println("Connecting to Wi-Fi (DHCP)...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  const IPAddress noIp(0, 0, 0, 0);
  const auto status = WiFi.status();
  const IPAddress currentIp = status == WL_CONNECTED ? WiFi.localIP() : noIp;
  PollWifiDiagnostics(status, currentIp != noIp);
  if (currentIp != lastIp) {
    lastIp = currentIp;
    if (currentIp == noIp) {
      Serial.println("Wi-Fi: IPv4 address lost.");
    } else {
      Serial.print("NodeMCU IPv4: ");
      Serial.println(currentIp);
      if (StartFrameHttpServer()) {
        Serial.println("Listening on HTTP port 80 at POST /frame.");
      } else {
        Serial.println("Failed to start HTTP frame server.");
      }
    }
  }

  if (currentIp != noIp) {
    PollFrameHttpServer(RenderFrame);
  }
  delay(5);
}
