#include "../../firmware/SmartMatrix/FrameHttpRequest.h"

#include <array>

namespace {

constexpr bool StopsBeforeBodyAtEverySplit() {
  constexpr std::string_view header = "POST /frame HTTP/1.1\r\nContent-Length: 768\r\n\r\n";
  constexpr std::string_view request = "POST /frame HTTP/1.1\r\nContent-Length: 768\r\n\r\nRGB\r\n\r\n";
  for (std::size_t split = 0; split <= header.size(); ++split) {
    matrix_http::HeaderEnd boundary;
    if (boundary.Scan(request.substr(0, split)) != split ||
        boundary.Scan(request.substr(split)) != header.size() - split || !boundary.Complete()) {
      return false;
    }
  }
  return true;
}

constexpr bool RespectsLimit(bool endsInTime) {
  constexpr auto limit = frame_http::MaxHeaderBytes;
  std::array<char, limit + 1> bytes{};
  const std::size_t start = endsInTime ? limit - 4 : limit - 3;
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[start + i] = "\r\n\r\n"[i];
  }
  matrix_http::HeaderEnd boundary;
  return boundary.Scan({bytes.data(), limit}) == limit && boundary.Complete() == endsInTime;
}

constexpr bool HandlesOverlappingPrefix() {
  matrix_http::HeaderEnd boundary;
  return boundary.Scan("\r\n\r") == 3 && !boundary.Complete() &&
      boundary.Scan("\r\n\r\npayload") == 4 && boundary.Complete();
}

static_assert(StopsBeforeBodyAtEverySplit());
static_assert(RespectsLimit(true));
static_assert(RespectsLimit(false));
static_assert(HandlesOverlappingPrefix());

}  // namespace
