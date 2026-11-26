#pragma once

#include <cstddef>

namespace provider_body {

enum class Framing { FixedLength, Chunked, UntilClose };
enum class State { Reading, Complete, Invalid, TooLarge, TimedOut };

constexpr int End = -1;
constexpr int Timeout = -2;

// HTTPClient consumes the headers, but exposes the undecoded body. Adapt it to
// ArduinoJson's reader API while bounding decoded bytes and chunk metadata.
template<class Input>
class Reader {
 public:
  constexpr Reader(Input& input, Framing framing, std::size_t length, std::size_t limit)
      : input_(input), framing_(framing),
        remaining_(framing == Framing::FixedLength ? length : 0), limit_(limit) {
    if (framing_ == Framing::FixedLength && length > limit_) {
      state_ = State::TooLarge;
    }
  }

  constexpr State state() const { return state_; }

  constexpr int read() {
    if (state_ != State::Reading) {
      return End;
    }
    if (framing_ == Framing::FixedLength && remaining_ == 0) {
      state_ = State::Complete;
      return End;
    }
    if (framing_ == Framing::Chunked && remaining_ == 0 && !NextChunk()) {
      return End;
    }
    const int byte = input_.read();
    if (byte < 0) {
      state_ = byte == Timeout ? State::TimedOut :
               framing_ == Framing::UntilClose ? State::Complete : State::Invalid;
      return End;
    }
    if (received_ == limit_) {
      state_ = State::TooLarge;
      return End;
    }
    ++received_;
    if (framing_ != Framing::UntilClose) {
      --remaining_;
    }
    return byte;
  }

  constexpr std::size_t readBytes(char* bytes, std::size_t length) {
    std::size_t count = 0;
    while (count < length) {
      const int byte = read();
      if (byte < 0) {
        break;
      }
      bytes[count++] = static_cast<char>(byte);
    }
    return count;
  }

  // ArduinoJson stops at the end of the object. Require the entire body before
  // using the frame, even if that object arrived before a truncated last chunk.
  constexpr void FinishJson() {
    for (int c = read(); c >= 0; c = read()) {
      if (c != ' ' && c != '\r' && c != '\n' && c != '\t') {
        state_ = State::Invalid;
        break;
      }
    }
  }

 private:
  static constexpr std::size_t MaxChunkLine = 128;
  static constexpr std::size_t MaxTrailers = 1024;

  static constexpr int HexDigit(int c) {
    return c >= '0' && c <= '9' ? c - '0' :
           c >= 'a' && c <= 'f' ? c - 'a' + 10 :
           c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
  }

  constexpr bool Invalid() {
    if (state_ == State::Reading) {
      state_ = State::Invalid;
    }
    return false;
  }

  constexpr int ReadFramingByte() {
    const int c = input_.read();
    if (c < 0) {
      state_ = c == Timeout ? State::TimedOut : State::Invalid;
    }
    return c;
  }

  constexpr bool Expect(char expected) {
    return ReadFramingByte() == expected || Invalid();
  }

  constexpr void FinishTrailers() {
    bool empty = true;
    for (std::size_t i = 0; i < MaxTrailers; ++i) {
      const int c = ReadFramingByte();
      if (c == '\r') {
        if (++i == MaxTrailers || !Expect('\n')) {
          break;
        }
        if (empty) {
          state_ = State::Complete;
          return;
        }
        empty = true;
      } else if (c < 0 || c == '\n') {
        break;
      } else {
        empty = false;
      }
    }
    Invalid();
  }

  constexpr bool NextChunk() {
    if (received_ != 0 && (!Expect('\r') || !Expect('\n'))) {
      return false;
    }

    const std::size_t budget = limit_ - received_;
    std::size_t chunk = 0;
    bool seenDigit = false;
    bool afterSize = false;
    bool extension = false;
    for (std::size_t i = 0; i < MaxChunkLine; ++i) {
      const int c = ReadFramingByte();
      if (c == '\r') {
        if (!seenDigit || i + 1 == MaxChunkLine || !Expect('\n')) {
          return Invalid();
        }
        if (chunk == 0) {
          FinishTrailers();
          return false;
        }
        remaining_ = chunk;
        return true;
      }
      if ((c < ' ' && c != '\t') || c == 127) {
        return Invalid();
      }
      if (extension) {
        continue;
      }
      if (seenDigit && c == ';') {
        extension = true;
        continue;
      }
      if (seenDigit && (c == ' ' || c == '\t')) {
        afterSize = true;
        continue;
      }
      const int digit = HexDigit(c);
      if (digit < 0 || afterSize) {
        return Invalid();
      }
      if (chunk > budget / 16 ||
          (chunk == budget / 16 && static_cast<std::size_t>(digit) > budget % 16)) {
        state_ = State::TooLarge;
        return false;
      }
      chunk = chunk * 16 + digit;
      seenDigit = true;
    }
    return Invalid();
  }

  Input& input_;
  Framing framing_;
  std::size_t remaining_;
  std::size_t limit_;
  std::size_t received_ = 0;
  State state_ = State::Reading;
};

}  // namespace provider_body
