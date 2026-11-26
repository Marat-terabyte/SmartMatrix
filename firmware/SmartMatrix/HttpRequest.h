#pragma once

#include <string_view>

namespace matrix_http {

// Return the bytes to consume, never including any bytes of the HTTP body.
class HeaderEnd {
 public:
  constexpr std::size_t Scan(std::string_view bytes) {
    constexpr std::string_view separator = "\r\n\r\n";
    if (Complete()) {
      return 0;
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      if (bytes[i] == separator[matched_]) {
        if (++matched_ == separator.size()) {
          return i + 1;
        }
      } else {
        matched_ = bytes[i] == '\r' ? 1 : 0;
      }
    }
    return bytes.size();
  }

  constexpr bool Complete() const { return matched_ == 4; }

 private:
  std::size_t matched_ = 0;
};

enum class Status {
  Ready,
  BadRequest,
  NotFound,
  MethodNotAllowed,
  Timeout,
  UnsupportedMediaType,
  ExpectationFailed,
  HeadersTooLarge
};

constexpr std::string_view TrimOws(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

constexpr char AsciiLower(char c) {
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

constexpr bool EqualsIgnoreCase(std::string_view value, std::string_view lowerCase) {
  if (value.size() != lowerCase.size()) {
    return false;
  }
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (AsciiLower(value[i]) != lowerCase[i]) {
      return false;
    }
  }
  return true;
}

struct RequestLine {
  std::string_view method;
  std::string_view path;
};

constexpr RequestLine ParseRequestLine(std::string_view headers) {
  const auto end = headers.find("\r\n");
  if (end == std::string_view::npos) {
    return {};
  }
  const auto request = headers.substr(0, end);
  const auto methodEnd = request.find(' ');
  const auto pathEnd = request.rfind(' ');
  if (methodEnd == std::string_view::npos || methodEnd == 0 || pathEnd <= methodEnd + 1) {
    return {};
  }
  const auto version = request.substr(pathEnd + 1);
  if (version != "HTTP/1.1" && version != "HTTP/1.0") {
    return {};
  }
  return {request.substr(0, methodEnd), request.substr(methodEnd + 1, pathEnd - methodEnd - 1)};
}

}  // namespace matrix_http
