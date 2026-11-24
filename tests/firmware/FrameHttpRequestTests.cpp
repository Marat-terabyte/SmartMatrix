#include "../../firmware/SmartMatrix/FrameHttpRequest.h"

using frame_http::ParseHeaders;
using frame_http::Status;

static_assert(frame_http::FrameBytes == 768);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\n"
                           "Host: 192.168.1.42\r\nContent-Type: application/octet-stream\r\n"
                           "Content-Length: 768\r\n\r\n") == Status::Ready);
static_assert(ParseHeaders("POST /frame HTTP/1.0\r\n"
                           "content-length:\t768 \r\nCONTENT-TYPE: Application/Octet-Stream\r\n\r\n") ==
              Status::Ready);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Type: application/octet-stream\r\n"
                           "Content-Length: 767\r\n\r\n") == Status::BadRequest);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 769\r\n\r\n") == Status::BadRequest);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 999999999999\r\n\r\n") == Status::BadRequest);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 768junk\r\n\r\n") == Status::BadRequest);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 768\r\n"
                           "Content-Length: 768\r\n\r\n") == Status::BadRequest);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 768\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n") == Status::BadRequest);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 768\r\n"
                           "Content-Type: application/octet-stream\r\n") == Status::BadRequest);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 768\r\n\r\n") == Status::UnsupportedMediaType);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 768\r\nContent-Type: application/json\r\n\r\n") ==
              Status::UnsupportedMediaType);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 768\r\n"
                           "Content-Type: application/octet-stream\r\nContent-Encoding: gzip\r\n\r\n") ==
              Status::UnsupportedMediaType);
static_assert(ParseHeaders("POST /frame HTTP/1.1\r\nContent-Length: 768\r\n"
                           "Content-Type: application/octet-stream\r\nExpect: 100-continue\r\n\r\n") ==
              Status::ExpectationFailed);
static_assert(ParseHeaders("POST /other HTTP/1.1\r\n\r\n") == Status::NotFound);
static_assert(ParseHeaders("GET /frame HTTP/1.1\r\n\r\n") == Status::MethodNotAllowed);
