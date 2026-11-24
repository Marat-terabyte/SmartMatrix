#include "FrameHttpServer.h"
#include "FrameHttpRequest.h"

#include <ESP8266WiFi.h>
#include <lwip/tcp.h>

namespace {

using frame_http::Status;

constexpr uint16_t Port = 80;
constexpr uint32_t RequestLimitMs = 5000;
static_assert(sizeof(matrix::Rgb) == 3, "Wire RGB triples must have no padding.");

WiFiServer server(Port);

void SendStatus(WiFiClient& client, Status status) {
  switch (status) {
    case Status::Ready:
      client.print(F("HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n"));
      return;
    case Status::BadRequest:
      client.print(F("HTTP/1.1 400 Bad Request\r\n"));
      break;
    case Status::NotFound:
      client.print(F("HTTP/1.1 404 Not Found\r\n"));
      break;
    case Status::MethodNotAllowed:
      client.print(F("HTTP/1.1 405 Method Not Allowed\r\nAllow: POST\r\n"));
      break;
    case Status::Timeout:
      client.print(F("HTTP/1.1 408 Request Timeout\r\n"));
      break;
    case Status::UnsupportedMediaType:
      client.print(F("HTTP/1.1 415 Unsupported Media Type\r\n"));
      break;
    case Status::ExpectationFailed:
      client.print(F("HTTP/1.1 417 Expectation Failed\r\n"));
      break;
    case Status::HeadersTooLarge:
      client.print(F("HTTP/1.1 431 Request Header Fields Too Large\r\n"));
      break;
  }
  client.print(F("Connection: close\r\nContent-Length: 0\r\n\r\n"));
}

bool WaitForData(WiFiClient& client, uint32_t started) {
  while (millis() - started < RequestLimitMs) {
    if (client.available() > 0) {
      return true;
    }
    if (!client.connected()) {
      return false;
    }
    delay(1);  // Let the ESP8266 process Wi-Fi events while waiting.
  }
  SendStatus(client, Status::Timeout);
  return false;
}

bool ReadHeaders(WiFiClient& client, uint32_t started) {
  static char headers[frame_http::MaxHeaderBytes];  // One request at a time in loop().
  for (size_t i = 0; i < sizeof(headers); ++i) {
    if (!WaitForData(client, started)) {
      return false;
    }
    const int next = client.read();
    if (next < 0) {
      return false;
    }
    headers[i] = static_cast<char>(next);
    if (i >= 3 && std::string_view(headers + i - 3, 4) == "\r\n\r\n") {
      const Status status = frame_http::ParseHeaders(std::string_view(headers, i + 1));
      if (status != Status::Ready) {
        SendStatus(client, status);
      }
      return status == Status::Ready;
    }
  }
  SendStatus(client, Status::HeadersTooLarge);
  return false;
}

void HandleClient(WiFiClient& client, FrameRenderer render) {
  const auto started = millis();
  if (!ReadHeaders(client, started)) {
    return;
  }

  matrix::Rgb frame[matrix::PixelCount];
  auto* bytes = reinterpret_cast<uint8_t*>(frame);
  size_t offset = 0;
  while (offset < frame_http::FrameBytes) {
    if (!WaitForData(client, started)) {
      return;
    }
    const int count = client.read(bytes + offset, frame_http::FrameBytes - offset);
    if (count <= 0) {
      return;
    }
    offset += static_cast<size_t>(count);
  }

  render(frame);
  SendStatus(client, Status::Ready);
}

}  // namespace

bool StartFrameHttpServer() {
  if (server.status() != LISTEN) {
    server.begin();
  }
  return server.status() == LISTEN;
}

void PollFrameHttpServer(FrameRenderer render) {
  auto client = server.accept();
  if (client) {
    client.setTimeout(500);
    HandleClient(client, render);
    client.stop(250);
  }
}
