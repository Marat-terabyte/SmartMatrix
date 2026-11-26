#pragma once

#include "HttpRequest.h"

namespace draw_http {

constexpr std::size_t MaxConfigBody = 6144;
constexpr std::size_t MaxDrawBody = 4800;
constexpr std::size_t MaxBaseUrl = 240;
constexpr std::size_t MaxModel = 128;
constexpr std::size_t MaxKey = 512;
constexpr std::size_t MaxCa = 4096;
constexpr std::size_t MaxPrompt = 768;

using matrix_http::Status;

constexpr bool MatchesAddress(std::string_view value, std::string_view ip) {
  if (ip.empty() || value.substr(0, ip.size()) != ip) {
    return false;
  }
  const auto port = value.substr(ip.size());
  return port.empty() || port == ":80";
}

constexpr Status ParseHeaders(std::string_view headers, std::string_view ip,
                              bool post, std::size_t maxBody, std::size_t& length) {
  auto end = headers.find("\r\n");
  if (end == std::string_view::npos) {
    return Status::BadRequest;
  }
  headers.remove_prefix(end + 2);
  bool hasHost = false;
  bool hasOrigin = false;
  bool hasLength = false;
  bool hasType = false;
  length = 0;
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
    if (matrix_http::EqualsIgnoreCase(name, "host")) {
      if (hasHost || !MatchesAddress(value, ip)) {
        return Status::BadRequest;
      }
      hasHost = true;
    } else if (post && matrix_http::EqualsIgnoreCase(name, "origin")) {
      if (hasOrigin || value.substr(0, 7) != "http://" || !MatchesAddress(value.substr(7), ip)) {
        return Status::BadRequest;
      }
      hasOrigin = true;
    } else if (matrix_http::EqualsIgnoreCase(name, "content-length")) {
      if (hasLength || value.empty()) {
        return Status::BadRequest;
      }
      hasLength = true;
      for (char c : value) {
        if (c < '0' || c > '9') {
          return Status::BadRequest;
        }
        length = length * 10 + (c - '0');
        if (length > maxBody) {
          return Status::BadRequest;
        }
      }
    } else if (matrix_http::EqualsIgnoreCase(name, "content-type")) {
      if (hasType || !matrix_http::EqualsIgnoreCase(value, "application/json")) {
        return Status::UnsupportedMediaType;
      }
      hasType = true;
    } else if (matrix_http::EqualsIgnoreCase(name, "transfer-encoding")) {
      return Status::BadRequest;
    } else if (matrix_http::EqualsIgnoreCase(name, "expect")) {
      return Status::ExpectationFailed;
    } else if (matrix_http::EqualsIgnoreCase(name, "content-encoding") &&
               !matrix_http::EqualsIgnoreCase(value, "identity")) {
      return Status::UnsupportedMediaType;
    }
  }
  if (headers != "\r\n" || !hasHost || (post ? !hasOrigin || length == 0 : length != 0)) {
    return Status::BadRequest;
  }
  return post && !hasType ? Status::UnsupportedMediaType : Status::Ready;
}

}  // namespace draw_http
