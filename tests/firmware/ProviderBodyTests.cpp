#include "../../firmware/SmartMatrix/ProviderBody.h"

#include <string_view>

namespace {

using provider_body::End;
using provider_body::Framing;
using provider_body::Reader;
using provider_body::State;
using provider_body::Timeout;

struct Input {
  std::string_view bytes;
  int eof = End;
  std::size_t offset = 0;

  constexpr int read() {
    return offset < bytes.size() ? static_cast<unsigned char>(bytes[offset++]) : eof;
  }
};

constexpr bool ReadsFixedLength() {
  Input input{"abcNEXT"};
  Reader body(input, Framing::FixedLength, 3, 3);
  char bytes[4]{};
  return body.readBytes(bytes, sizeof(bytes)) == 3 && std::string_view(bytes) == "abc" &&
      input.offset == 3 && body.state() == State::Complete;
}

constexpr bool ReadsChunked() {
  Input input{"3;part=one\r\nabc\r\n5\r\ndefgh\r\n0\r\nX-Status: done\r\n\r\nNEXT"};
  Reader body(input, Framing::Chunked, 0, 8);
  char bytes[9]{};
  return body.readBytes(bytes, sizeof(bytes)) == 8 && std::string_view(bytes) == "abcdefgh" &&
      input.bytes.substr(input.offset) == "NEXT" && body.state() == State::Complete;
}

constexpr State Consume(std::string_view bytes, Framing framing, std::size_t length,
                        std::size_t limit, int eof = End) {
  Input input{bytes, eof};
  Reader body(input, framing, length, limit);
  while (body.read() >= 0) {
  }
  return body.state();
}

constexpr State SimulateJson(std::string_view raw, Framing framing, std::size_t length,
                             int eof = End) {
  Input input{raw, eof};
  Reader body(input, framing, length, 64);
  if (body.read() != '{' || body.read() != '}') {
    return State::Invalid;
  }
  body.FinishJson();
  return body.state();
}

constexpr bool BoundsChunkMetadata() {
  char raw[160]{};
  for (auto& c : raw) {
    c = 'x';
  }
  raw[0] = '1';
  raw[1] = ';';
  return Consume({raw, sizeof(raw)}, Framing::Chunked, 0, 1) == State::Invalid;
}

constexpr bool ReadsFullSizedChunk() {
  constexpr std::size_t limit = 8192;
  constexpr std::string_view prefix = "2000\r\n";  // 8192 in hexadecimal.
  constexpr std::string_view suffix = "\r\n0\r\n\r\n";
  char raw[prefix.size() + limit + suffix.size()]{};
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    raw[i] = prefix[i];
  }
  for (std::size_t i = 0; i < limit; ++i) {
    raw[prefix.size() + i] = 'x';
  }
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    raw[prefix.size() + limit + i] = suffix[i];
  }
  return Consume({raw, sizeof(raw)}, Framing::Chunked, 0, limit) == State::Complete;
}

static_assert(ReadsFixedLength());
static_assert(ReadsChunked());
static_assert(BoundsChunkMetadata());
static_assert(ReadsFullSizedChunk());
static_assert(Consume("abc", Framing::FixedLength, 4, 3) == State::TooLarge);
static_assert(Consume("abc", Framing::FixedLength, 4, 4) == State::Invalid);
static_assert(Consume("abc", Framing::FixedLength, 4, 4, Timeout) == State::TimedOut);
static_assert(Consume("abc", Framing::UntilClose, 0, 3) == State::Complete);
static_assert(Consume("abcd", Framing::UntilClose, 0, 3) == State::TooLarge);
static_assert(Consume("abc", Framing::UntilClose, 0, 3, Timeout) == State::TimedOut);
static_assert(Consume("1\r\na\r\n0\r\n\r\n", Framing::Chunked, 99, 1) == State::Complete);
static_assert(Consume("1000000000000000000\r\n", Framing::Chunked, 0, 8192) == State::TooLarge);
static_assert(Consume("2\r\nab\r\n2\r\ncd\r\n0\r\n\r\n", Framing::Chunked, 0, 3) == State::TooLarge);
static_assert(Consume("z\r\nabc\r\n0\r\n\r\n", Framing::Chunked, 0, 12) == State::Invalid);
static_assert(Consume("1\r\naX\r\n0\r\n\r\n", Framing::Chunked, 0, 12) == State::Invalid);
static_assert(SimulateJson("{}\r\n", Framing::FixedLength, 4) == State::Complete);
static_assert(SimulateJson("{}\r\n", Framing::FixedLength, 6) == State::Invalid);
static_assert(SimulateJson("{}X", Framing::FixedLength, 3) == State::Invalid);
static_assert(SimulateJson("2\r\n{}\r\n1\r\n \r\n0\r\n\r\n", Framing::Chunked, 0) == State::Complete);
static_assert(SimulateJson("2\r\n{}\r\n0\r\n", Framing::Chunked, 0) == State::Invalid);
static_assert(SimulateJson("2\r\n{}\r\n0\r\n", Framing::Chunked, 0, Timeout) == State::TimedOut);

}  // namespace
