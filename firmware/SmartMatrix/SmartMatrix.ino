#include <FastLED.h>
#include "MatrixHttpServer.h"
#include "WifiProvisioning.h"

// Change these after the matrix is wired.
constexpr uint8_t DataPin = D5;  // GPIO14 on NodeMCU 1.0
constexpr uint8_t Brightness = 64;
constexpr auto FirstPixel = matrix::FirstCorner::TopLeft;
constexpr bool ShowStartupTest = false;

CRGB leds[matrix::PixelCount];

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

  wifi_setup::Begin();
}

void loop() {
  wifi_setup::Poll();
  PollMatrixHttpServer(RenderFrame);
  delay(5);
}
