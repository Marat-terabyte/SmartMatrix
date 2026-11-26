#pragma once

#include "HttpRequest.h"
#include "MatrixLayout.h"

#include <string_view>

namespace model_api {

struct ProviderUrl {
  bool tls;
  uint16_t port;
  std::string_view host;
  std::string_view path;
};

constexpr bool AlphaNumeric(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

constexpr bool ParseProviderUrl(std::string_view url, ProviderUrl& output) {
  const bool tls = matrix_http::EqualsIgnoreCase(url.substr(0, 8), "https://");
  if (!tls && !matrix_http::EqualsIgnoreCase(url.substr(0, 7), "http://")) {
    return false;
  }
  url.remove_prefix(tls ? 8 : 7);
  const auto slash = url.find('/');
  const auto authority = url.substr(0, slash);
  const auto colon = authority.find(':');
  const auto host = authority.substr(0, colon);
  if (host.empty()) {
    return false;
  }
  auto remaining = host;
  while (!remaining.empty()) {
    const auto dot = remaining.find('.');
    const auto label = remaining.substr(0, dot);
    if (label.empty() || label.size() > 63 ||
        !AlphaNumeric(label.front()) || !AlphaNumeric(label.back())) {
      return false;
    }
    for (const char c : label) {
      if (!AlphaNumeric(c) && c != '-') {
        return false;
      }
    }
    if (dot == std::string_view::npos) {
      break;
    }
    remaining.remove_prefix(dot + 1);
    if (remaining.empty()) {
      return false;
    }
  }

  unsigned port = tls ? 443 : 80;
  if (colon != std::string_view::npos) {
    const auto number = authority.substr(colon + 1);
    port = 0;
    for (const char c : number) {
      if (c < '0' || c > '9') {
        return false;
      }
      port = port * 10 + (c - '0');
      if (port > 65535) {
        return false;
      }
    }
    if (port == 0) {
      return false;
    }
  }

  auto path = slash == std::string_view::npos ? std::string_view{} : url.substr(slash);
  for (const char c : path) {
    if (!AlphaNumeric(c) && std::string_view("/-._~").find(c) == std::string_view::npos) {
      return false;
    }
  }
  while (!path.empty() && path.back() == '/') {
    path.remove_suffix(1);
  }
  output = {tls, static_cast<uint16_t>(port), host, path};
  return true;
}

struct PaletteEntry {
  char symbol;
  matrix::Rgb rgb;
};

// Keep in sync with the CLI palette and docs/protocol.md.
constexpr PaletteEntry Palette[] = {
  {'.', {0, 0, 0}}, {'W', {255, 255, 255}}, {'R', {255, 0, 0}},
  {'O', {255, 128, 0}}, {'Y', {255, 255, 0}}, {'G', {0, 255, 0}},
  {'C', {0, 255, 255}}, {'B', {0, 0, 255}}, {'P', {160, 0, 255}},
  {'M', {255, 0, 255}}, {'S', {255, 204, 153}}, {'N', {160, 82, 45}},
  {'A', {128, 128, 128}}, {'D', {64, 64, 64}}
};

namespace frame_json {

constexpr void SkipWhitespace(std::string_view& json) {
  while (!json.empty() && std::string_view(" \t\r\n").find(json.front()) != std::string_view::npos) {
    json.remove_prefix(1);
  }
}

constexpr bool Take(std::string_view& json, char expected) {
  SkipWhitespace(json);
  if (json.empty() || json.front() != expected) {
    return false;
  }
  json.remove_prefix(1);
  return true;
}

// Only printable ASCII is needed for the field name and the palette.
constexpr char ReadAscii(std::string_view& json) {
  if (json.empty() || json.front() == '"') {
    return '\0';
  }
  const char c = json.front();
  json.remove_prefix(1);
  if (c != '\\') {
    return c;
  }
  if (json.size() < 5 || json.front() != 'u') {
    return '\0';
  }
  unsigned value = 0;
  for (std::size_t i = 1; i <= 4; ++i) {
    const char digit = json[i];
    if (digit >= '0' && digit <= '9') {
      value = value * 16 + digit - '0';
    } else {
      const char lower = digit >= 'A' && digit <= 'F' ? digit + ('a' - 'A') : digit;
      if (lower < 'a' || lower > 'f') {
        return '\0';
      }
      value = value * 16 + lower - 'a' + 10;
    }
  }
  json.remove_prefix(5);
  return value >= 32 && value < 127 ? static_cast<char>(value) : '\0';
}

constexpr bool EndString(std::string_view& json) {
  if (json.empty() || json.front() != '"') {
    return false;
  }
  json.remove_prefix(1);
  return true;
}

}  // namespace frame_json

// The caller may render the output only on success. Reject duplicate/extra fields,
// shortened or extended rows, and any text before or after the JSON object.
constexpr bool ParseFrame(std::string_view json, matrix::Rgb (&frame)[matrix::PixelCount]) {
  using namespace frame_json;
  if (!Take(json, '{') || !Take(json, '"')) {
    return false;
  }
  for (const char c : std::string_view("rows")) {
    if (ReadAscii(json) != c) {
      return false;
    }
  }
  if (!EndString(json) || !Take(json, ':') || !Take(json, '[')) {
    return false;
  }
  for (uint16_t y = 0; y < matrix::Height; ++y) {
    if ((y != 0 && !Take(json, ',')) || !Take(json, '"')) {
      return false;
    }
    for (uint16_t x = 0; x < matrix::Width; ++x) {
      const char symbol = ReadAscii(json);
      const PaletteEntry* color = nullptr;
      for (const auto& entry : Palette) {
        if (symbol == entry.symbol) {
          color = &entry;
          break;
        }
      }
      if (!color) {
        return false;
      }
      frame[y * matrix::Width + x] = color->rgb;
    }
    if (!EndString(json)) {
      return false;
    }
  }
  if (!Take(json, ']') || !Take(json, '}')) {
    return false;
  }
  SkipWhitespace(json);
  return json.empty();
}

}  // namespace model_api
