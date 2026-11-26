#include "../../firmware/SmartMatrix/ModelFrame.h"

using model_api::ParseFrame;
using model_api::ParseProviderUrl;
using model_api::ProviderUrl;

namespace {

constexpr bool Addresses() {
  ProviderUrl url{};
  return ParseProviderUrl("http://192.168.1.10:11434/v1/", url) &&
      !url.tls && url.port == 11434 && url.host == "192.168.1.10" && url.path == "/v1" &&
      ParseProviderUrl("https://api.openai.com/v1", url) &&
      url.tls && url.port == 443 && url.host == "api.openai.com" && url.path == "/v1" &&
      ParseProviderUrl("HTTPS://provider.test:8443", url) &&
      url.tls && url.port == 8443 && url.path.empty() &&
      ParseProviderUrl("http://localhost", url) && !url.tls && url.port == 80 &&
      !ParseProviderUrl("ftp://provider.test", url) &&
      !ParseProviderUrl("https://key@provider.test/v1", url) &&
      !ParseProviderUrl("https://provider.test/v1?api_key=test", url) &&
      !ParseProviderUrl("https://provider.test/v1#fragment", url) &&
      !ParseProviderUrl("https://provider.test:0/v1", url) &&
      !ParseProviderUrl("https://provider.test:65536/v1", url) &&
      !ParseProviderUrl("https://provider.test\r\n/v1", url);
}

#define BLACK "\"................\","
#define FULL_ROWS "\"\\u0053WGOB...........\"," \
    BLACK BLACK BLACK BLACK BLACK BLACK BLACK BLACK BLACK BLACK BLACK BLACK BLACK BLACK \
    "\"...............N\""

constexpr bool DetectsFrame() {
  matrix::Rgb frame[matrix::PixelCount]{};
  return ParseFrame(" \n{\"rows\":[" FULL_ROWS "]}\t", frame) &&
      frame[0].r == 255 && frame[0].g == 204 && frame[0].b == 153 &&
      frame[1].r == 255 && frame[1].g == 255 && frame[1].b == 255 &&
      frame[3].r == 255 && frame[3].g == 128 && frame[3].b == 0 &&
      frame[4].r == 0 && frame[4].g == 0 && frame[4].b == 255 &&
      frame[16].r == 0 && frame[16].g == 0 && frame[16].b == 0 &&
      frame[255].r == 160 && frame[255].g == 82 && frame[255].b == 45;
}

constexpr bool Rejects(std::string_view json) {
  matrix::Rgb frame[matrix::PixelCount]{};
  return !ParseFrame(json, frame);
}

static_assert(Addresses());
static_assert(DetectsFrame());
static_assert(Rejects("{\"rows\":[" FULL_ROWS "],\"rows\":[]}"));
static_assert(Rejects("{\"rows\":[" FULL_ROWS "],\"extra\":1}"));
static_assert(Rejects("{\"rows\":[" FULL_ROWS "]} trailing"));
static_assert(Rejects("```json\n{\"rows\":[" FULL_ROWS "]}\n```"));
static_assert(Rejects("{\"rows\":[\"................\"]}"));
static_assert(Rejects("{\"rows\":[" BLACK FULL_ROWS "]}"));
static_assert(Rejects("{\"rows\":[\"?...............\"," FULL_ROWS "]}"));
static_assert(Rejects("{\"rows\":[\"...............\"," FULL_ROWS "]}"));
static_assert(Rejects("{\"rows\":[\".................\"," FULL_ROWS "]}"));

#undef FULL_ROWS
#undef BLACK

}  // namespace
