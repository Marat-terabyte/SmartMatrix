#pragma once

#include <stdint.h>

namespace matrix {

constexpr uint16_t Width = 16;
constexpr uint16_t Height = 16;
constexpr uint16_t PixelCount = Width * Height;

enum class FirstCorner {
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight
};

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// x and y are logical coordinates measured from the top left.
constexpr uint16_t LedIndex(uint16_t x, uint16_t y, FirstCorner corner) {
  const bool startsAtBottom = corner == FirstCorner::BottomLeft ||
                              corner == FirstCorner::BottomRight;
  const bool startsAtRight = corner == FirstCorner::TopRight ||
                             corner == FirstCorner::BottomRight;
  const uint16_t row = startsAtBottom ? Height - 1 - y : y;
  const bool rightToLeft = startsAtRight != (row % 2 != 0);
  const uint16_t column = rightToLeft ? Width - 1 - x : x;
  return row * Width + column;
}

}  // namespace matrix
