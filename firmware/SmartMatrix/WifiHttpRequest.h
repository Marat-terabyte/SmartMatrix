#pragma once

#include "HttpRequest.h"

namespace wifi_http {

constexpr std::size_t MaxSsidBytes = 32;
constexpr std::size_t MaxPasswordBytes = 64;
constexpr std::size_t TokenBytes = 32;
constexpr std::size_t MaxFormBytes = 384;

struct Credentials {
  char ssid[MaxSsidBytes + 1] = {};
  char password[MaxPasswordBytes + 1] = {};
};

using matrix_http::Status;

constexpr Status ParseSetupHeaders(std::string_view headers, bool form, std::size_t& length) {
  auto end = headers.find("\r\n");
  if (end == std::string_view::npos) {
    return Status::BadRequest;
  }
  headers.remove_prefix(end + 2);
  length = 0;
  bool hasLength = false;
  bool hasType = false;
  bool hasHost = false;
  while ((end = headers.find("\r\n")) != std::string_view::npos && end != 0) {
    const auto line = headers.substr(0, end);
    headers.remove_prefix(end + 2);
    const auto colon = line.find(':');
    if (colon == 0 || colon == std::string_view::npos) {
      return Status::BadRequest;
    }
    const auto name = line.substr(0, colon);
    for (unsigned char c : name) {
      if (c <= ' ' || c >= 127) {
        return Status::BadRequest;
      }
    }
    const auto value = matrix_http::TrimOws(line.substr(colon + 1));
    if (matrix_http::EqualsIgnoreCase(name, "content-length")) {
      if (hasLength) {
        return Status::BadRequest;
      }
      hasLength = true;
      for (char c : value) {
        if (c < '0' || c > '9') {
          return Status::BadRequest;
        }
        length = length * 10 + (c - '0');
        if (length > MaxFormBytes) {
          return Status::BadRequest;
        }
      }
    } else if (matrix_http::EqualsIgnoreCase(name, "content-type")) {
      if (hasType || !matrix_http::EqualsIgnoreCase(value, "application/x-www-form-urlencoded")) {
        return Status::UnsupportedMediaType;
      }
      hasType = true;
    } else if (matrix_http::EqualsIgnoreCase(name, "host")) {
      if (hasHost || (value != "192.168.4.1" && value != "192.168.4.1:80")) {
        return Status::BadRequest;
      }
      hasHost = true;
    } else if (matrix_http::EqualsIgnoreCase(name, "transfer-encoding")) {
      return Status::BadRequest;
    } else if (matrix_http::EqualsIgnoreCase(name, "expect")) {
      return Status::ExpectationFailed;
    } else if (matrix_http::EqualsIgnoreCase(name, "content-encoding") &&
               !matrix_http::EqualsIgnoreCase(value, "identity")) {
      return Status::UnsupportedMediaType;
    }
  }
  if (headers != "\r\n" || !hasHost || (form ? length == 0 : length != 0)) {
    return Status::BadRequest;
  }
  return !form || hasType ? Status::Ready : Status::UnsupportedMediaType;
}

constexpr int HexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  c = matrix_http::AsciiLower(c);
  return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

template<std::size_t N>
constexpr bool Decode(std::string_view input, char (&output)[N]) {
  std::size_t written = 0;
  for (std::size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (c == '+') {
      c = ' ';
    } else if (c == '%') {
      if (input.size() - i < 3) {
        return false;
      }
      const int high = HexDigit(input[++i]);
      const int low = HexDigit(input[++i]);
      if (high < 0 || low < 0) {
        return false;
      }
      c = static_cast<char>(high * 16 + low);
    }
    if (c == '\0' || written == N - 1) {
      return false;
    }
    output[written++] = c;
  }
  output[written] = '\0';
  return true;
}

template<std::size_t N>
constexpr std::size_t BoundedLength(const char (&value)[N]) {
  std::size_t length = 0;
  while (length < N && value[length] != '\0') {
    ++length;
  }
  return length;
}

constexpr bool ValidCredentials(const Credentials& credentials) {
  const auto ssidLength = BoundedLength(credentials.ssid);
  const auto passwordLength = BoundedLength(credentials.password);
  if (ssidLength == 0 || ssidLength > MaxSsidBytes || passwordLength > MaxPasswordBytes ||
      (passwordLength != 0 && passwordLength < 8)) {
    return false;
  }
  for (std::size_t i = 0; i < ssidLength; ++i) {
    const auto c = static_cast<unsigned char>(credentials.ssid[i]);
    if (c < ' ' || c == 127) {
      return false;
    }
  }
  for (std::size_t i = 0; i < passwordLength; ++i) {
    const auto c = credentials.password[i];
    if (passwordLength == MaxPasswordBytes ? HexDigit(c) < 0 : (c < ' ' || c > '~')) {
      return false;
    }
  }
  return true;
}

constexpr bool ParseForm(std::string_view body, std::string_view token, Credentials& output) {
  Credentials credentials{};
  bool hasSsid = false;
  bool hasPassword = false;
  bool hasToken = false;
  while (!body.empty()) {
    const auto delimiter = body.find('&');
    const auto field = body.substr(0, delimiter);
    const auto equals = field.find('=');
    if (equals == std::string_view::npos) {
      return false;
    }
    const auto name = field.substr(0, equals);
    const auto value = field.substr(equals + 1);
    if (name == "ssid" && !hasSsid && Decode(value, credentials.ssid)) {
      hasSsid = true;
    } else if (name == "password" && !hasPassword && Decode(value, credentials.password)) {
      hasPassword = true;
    } else if (name == "token" && !hasToken && value == token) {
      hasToken = true;
    } else {
      return false;
    }
    if (delimiter == std::string_view::npos) {
      break;
    }
    body.remove_prefix(delimiter + 1);
    if (body.empty()) {
      return false;
    }
  }
  if (!hasSsid || !hasPassword || !hasToken || !ValidCredentials(credentials)) {
    return false;
  }
  output = credentials;
  return true;
}

}  // namespace wifi_http
