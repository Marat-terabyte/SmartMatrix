#include "../../firmware/SmartMatrix/DrawHttpRequest.h"

using draw_http::ParseHeaders;
using draw_http::Status;

namespace {

constexpr auto Ip = "192.168.1.42";

constexpr bool DrawHeadersSetLength() {
  std::size_t length = 0;
  return ParseHeaders(
      "POST /api/draw HTTP/1.1\r\nHost: 192.168.1.42\r\nOrigin: http://192.168.1.42\r\n"
      "Content-Length: 20\r\nContent-Type: application/json\r\n\r\n",
      Ip, true, draw_http::MaxDrawBody, length) == Status::Ready && length == 20;
}

constexpr Status Check(std::string_view headers, bool post = true) {
  std::size_t length = 0;
  return ParseHeaders(headers, Ip, post, draw_http::MaxConfigBody, length);
}

static_assert(DrawHeadersSetLength());
static_assert(Check("GET / HTTP/1.1\r\nHost: 192.168.1.42:80\r\n\r\n", false) == Status::Ready);
static_assert(Check("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n", false) == Status::BadRequest);
static_assert(Check("GET / HTTP/1.1\r\nHost: 192.168.1.420\r\n\r\n", false) == Status::BadRequest);
static_assert(Check("POST /api/config HTTP/1.1\r\nHost: 192.168.1.42\r\nContent-Length: 20\r\n"
                    "Content-Type: application/json\r\n\r\n") == Status::BadRequest);
static_assert(Check("POST /api/config HTTP/1.1\r\nHost: 192.168.1.42\r\nOrigin: http://example.com\r\n"
                    "Content-Length: 20\r\nContent-Type: application/json\r\n\r\n") == Status::BadRequest);
static_assert(Check("POST /api/config HTTP/1.1\r\nHost: 192.168.1.42\r\nOrigin: http://192.168.1.42\r\n"
                    "Content-Length: 20\r\nContent-Type: text/plain\r\n\r\n") == Status::UnsupportedMediaType);
static_assert(Check("POST /api/config HTTP/1.1\r\nHost: 192.168.1.42\r\nOrigin: http://192.168.1.42\r\n"
                    "Content-Length: 20\r\nContent-Length: 20\r\n"
                    "Content-Type: application/json\r\n\r\n") == Status::BadRequest);
static_assert(Check("POST /api/config HTTP/1.1\r\nHost: 192.168.1.42\r\nOrigin: http://192.168.1.42\r\n"
                    "Content-Length: 6145\r\nContent-Type: application/json\r\n\r\n") == Status::BadRequest);

}  // namespace
