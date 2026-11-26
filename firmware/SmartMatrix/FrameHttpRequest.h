#pragma once

#include <cstddef>
#include <string_view>
#include "HttpRequest.h"
#include "MatrixLayout.h"

namespace frame_http {

constexpr std::size_t FrameBytes = matrix::PixelCount * 3;
constexpr std::size_t MaxHeaderBytes = 1024;

using matrix_http::Status;

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
  const auto request = matrix_http::ParseRequestLine(headers);
  if (request.method.empty()) {
    return Status::BadRequest;
  }
  if (request.path != "/frame") {
    return Status::NotFound;
  }
  if (request.method != "POST") {
    return Status::MethodNotAllowed;
  }

  auto end = headers.find("\r\n");
  headers.remove_prefix(end + 2);
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
    const auto value = matrix_http::TrimOws(line.substr(colon + 1));
    if (matrix_http::EqualsIgnoreCase(name, "content-length")) {
      if (hasLength || !IsFrameLength(value)) {
        return Status::BadRequest;
      }
      hasLength = true;
    } else if (matrix_http::EqualsIgnoreCase(name, "transfer-encoding")) {
      return Status::BadRequest;
    } else if (matrix_http::EqualsIgnoreCase(name, "content-type")) {
      if (hasType || !matrix_http::EqualsIgnoreCase(value, "application/octet-stream")) {
        return Status::UnsupportedMediaType;
      }
      hasType = true;
    } else if (matrix_http::EqualsIgnoreCase(name, "expect")) {
      expectsMore = true;
    } else if (matrix_http::EqualsIgnoreCase(name, "content-encoding") &&
               !matrix_http::EqualsIgnoreCase(value, "identity")) {
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
