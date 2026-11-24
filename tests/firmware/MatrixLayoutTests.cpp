#include "../../firmware/SmartMatrix/MatrixLayout.h"

using matrix::FirstCorner;
using matrix::LedIndex;

constexpr bool UsesEveryLedOnce(FirstCorner corner) {
  bool seen[matrix::PixelCount] = {};
  for (uint16_t y = 0; y < matrix::Height; ++y) {
    for (uint16_t x = 0; x < matrix::Width; ++x) {
      const uint16_t index = LedIndex(x, y, corner);
      if (index >= matrix::PixelCount || seen[index]) {
        return false;
      }
      seen[index] = true;
    }
  }
  return true;
}

static_assert(UsesEveryLedOnce(FirstCorner::TopLeft));
static_assert(UsesEveryLedOnce(FirstCorner::TopRight));
static_assert(UsesEveryLedOnce(FirstCorner::BottomLeft));
static_assert(UsesEveryLedOnce(FirstCorner::BottomRight));

static_assert(LedIndex(0, 0, FirstCorner::TopLeft) == 0);
static_assert(LedIndex(15, 0, FirstCorner::TopLeft) == 15);
static_assert(LedIndex(15, 1, FirstCorner::TopLeft) == 16);
static_assert(LedIndex(0, 1, FirstCorner::TopLeft) == 31);
static_assert(LedIndex(15, 0, FirstCorner::TopRight) == 0);
static_assert(LedIndex(0, 15, FirstCorner::BottomLeft) == 0);
static_assert(LedIndex(15, 15, FirstCorner::BottomRight) == 0);
