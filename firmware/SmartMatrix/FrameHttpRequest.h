#pragma once

#include <cstddef>
#include <string_view>
#include "MatrixLayout.h"

namespace frame_http {

constexpr std::size_t FrameBytes = matrix::PixelCount * 3;
constexpr std::size_t MaxHeaderBytes = 1024;

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

constexpr bool IsFrameLength(std::string_view value) {
  std::size_t length = 0;
  for (char c : value) {
    if (c < '0' || c > '9') {
      return false;
    }
    length = length * 10 + (c - '0');
    if (length > FrameBytes) {
      return false;
    }
  }
  return length == FrameBytes;
}

// Takes only the request line and headers, including the final CRLFCRLF.
constexpr Status ParseHeaders(std::string_view headers) {
  auto end = headers.find("\r\n");
  if (end == std::string_view::npos) {
    return Status::BadRequest;
  }
  const auto request = headers.substr(0, end);
  headers.remove_prefix(end + 2);
  const auto methodEnd = request.find(' ');
  const auto pathEnd = request.rfind(' ');
  if (methodEnd == std::string_view::npos || methodEnd == 0 || pathEnd <= methodEnd + 1) {
    return Status::BadRequest;
  }
  const auto version = request.substr(pathEnd + 1);
  if (version != "HTTP/1.1" && version != "HTTP/1.0") {
    return Status::BadRequest;
  }
  if (request.substr(methodEnd + 1, pathEnd - methodEnd - 1) != "/frame") {
    return Status::NotFound;
  }
  if (request.substr(0, methodEnd) != "POST") {
    return Status::MethodNotAllowed;
  }

  bool hasLength = false;
  bool hasType = false;
  bool expectsMore = false;
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
    const auto value = TrimOws(line.substr(colon + 1));
    if (EqualsIgnoreCase(name, "content-length")) {
      if (hasLength || !IsFrameLength(value)) {
        return Status::BadRequest;
      }
      hasLength = true;
    } else if (EqualsIgnoreCase(name, "transfer-encoding")) {
      return Status::BadRequest;
    } else if (EqualsIgnoreCase(name, "content-type")) {
      if (hasType || !EqualsIgnoreCase(value, "application/octet-stream")) {
        return Status::UnsupportedMediaType;
      }
      hasType = true;
    } else if (EqualsIgnoreCase(name, "expect")) {
      expectsMore = true;
    } else if (EqualsIgnoreCase(name, "content-encoding") && !EqualsIgnoreCase(value, "identity")) {
      return Status::UnsupportedMediaType;
    }
  }
  if (headers != "\r\n" || !hasLength) {
    return Status::BadRequest;
  }
  if (!hasType) {
    return Status::UnsupportedMediaType;
  }
  return expectsMore ? Status::ExpectationFailed : Status::Ready;
}

}  // namespace frame_http
