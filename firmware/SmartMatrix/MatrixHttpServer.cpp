#include "MatrixHttpServer.h"
#include "DrawHttpRequest.h"
#include "DrawPage.h"
#include "FrameHttpRequest.h"
#include "ModelApi.h"
#include "WifiProvisioning.h"

#include <ESP8266WiFi.h>
#include <lwip/tcp.h>
#include <memory>
#include <new>

namespace {

using matrix_http::Status;

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

void SendWifiHeaders(WiFiClient& client) {
  client.print(F("Connection: close\r\n"
                 "Cache-Control: no-store\r\n"
                 "Referrer-Policy: no-referrer\r\n"
                 "Content-Security-Policy: default-src 'none'; form-action 'self'; "
                 "base-uri 'none'; frame-ancestors 'none'\r\n"));
}

void SendWifiPage(WiFiClient& client, bool fromSetup, const __FlashStringHelper* error = nullptr) {
  String page;
  page.reserve(1024);
  page += F("<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>SmartMatrix Wi-Fi</title>");
  const auto phase = wifi_setup::CurrentPhase();
  if (fromSetup && (phase == wifi_setup::Phase::Queued || phase == wifi_setup::Phase::Connecting)) {
    page += F("<meta http-equiv=\"refresh\" content=\"2;url=/wifi\">");
  }
  page += F("<h1>SmartMatrix Wi-Fi</h1><p>");
  page += error ? error : wifi_setup::StatusMessage();
  page += F("</p>");
  const IPAddress stationIp = wifi_setup::StationIp();
  if (stationIp != IPAddress(0, 0, 0, 0)) {
    page += F("<p>IPv4: <strong>");
    page += stationIp.toString();
    page += F("</strong></p>");
    if (fromSetup) {
      page += F("<p>The setup access point will turn off shortly. "
                "Reconnect to your network and open <code>http://");
      page += stationIp.toString();
      page += F("/wifi</code>.</p>");
    }
  }
  if (fromSetup && wifi_setup::CanConfigure()) {
    page += F("<form method=\"post\" action=\"/wifi\" accept-charset=\"UTF-8\">"
              "<input type=\"hidden\" name=\"token\" value=\"");
    page += wifi_setup::FormToken().data();
    page += F("\"><p><label>Network name (SSID) "
              "<input name=\"ssid\" maxlength=\"32\" required></label></p>"
              "<p><label>Wi-Fi password <input type=\"password\" name=\"password\" "
              "maxlength=\"64\" autocomplete=\"new-password\"></label> "
              "(leave blank only for an open network)</p>"
              "<button type=\"submit\">Connect</button></form>");
  }
  page += F("</html>");

  client.print(error ? F("HTTP/1.1 400 Bad Request\r\n") : F("HTTP/1.1 200 OK\r\n"));
  SendWifiHeaders(client);
  client.print(F("Content-Type: text/html; charset=utf-8\r\nContent-Length: "));
  client.print(page.length());
  client.print(F("\r\n\r\n"));
  client.print(page);
}

void RedirectToWifi(WiFiClient& client) {
  client.print(F("HTTP/1.1 303 See Other\r\nLocation: /wifi\r\n"));
  SendWifiHeaders(client);
  client.print(F("Content-Length: 0\r\n\r\n"));
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

std::string_view ReadHeaders(WiFiClient& client, uint32_t started) {
  static char headers[frame_http::MaxHeaderBytes];  // One request at a time in loop().
  for (size_t i = 0; i < sizeof(headers); ++i) {
    if (!WaitForData(client, started)) {
      return {};
    }
    const int next = client.read();
    if (next < 0) {
      return {};
    }
    headers[i] = static_cast<char>(next);
    if (i >= 3 && std::string_view(headers + i - 3, 4) == "\r\n\r\n") {
      return {headers, i + 1};
    }
  }
  SendStatus(client, Status::HeadersTooLarge);
  return {};
}

bool ReadBody(WiFiClient& client, uint32_t started, uint8_t* bytes, size_t length) {
  size_t offset = 0;
  while (offset < length) {
    if (!WaitForData(client, started)) {
      return false;
    }
    const int count = client.read(bytes + offset, length - offset);
    if (count <= 0) {
      return false;
    }
    offset += static_cast<size_t>(count);
  }
  return true;
}

void SendDrawHeaders(WiFiClient& client, const __FlashStringHelper* type, size_t length) {
  client.print(F("Connection: close\r\nCache-Control: no-store\r\nReferrer-Policy: same-origin\r\n"
                 "X-Content-Type-Options: nosniff\r\n"
                 "Content-Security-Policy: default-src 'none'; script-src 'self'; connect-src 'self'; "
                 "style-src 'unsafe-inline'; form-action 'none'; base-uri 'none'; frame-ancestors 'none'\r\n"
                 "Content-Type: "));
  client.print(type);
  client.print(F("\r\nContent-Length: "));
  client.print(length);
  client.print(F("\r\n\r\n"));
}

void SendDrawAsset(WiFiClient& client, const __FlashStringHelper* type, const char* asset, size_t length) {
  client.print(F("HTTP/1.1 200 OK\r\n"));
  SendDrawHeaders(client, type, length);
  client.write_P(asset, length);
}

void SendDrawError(WiFiClient& client, model_api::Result result) {
  switch (result.status) {
    case 400: client.print(F("HTTP/1.1 400 Bad Request\r\n")); break;
    case 409: client.print(F("HTTP/1.1 409 Conflict\r\n")); break;
    case 503: client.print(F("HTTP/1.1 503 Service Unavailable\r\n")); break;
    case 504: client.print(F("HTTP/1.1 504 Gateway Timeout\r\n")); break;
    default: client.print(F("HTTP/1.1 502 Bad Gateway\r\n")); break;
  }
  char suffix[24] = {};
  if (result.providerStatus) {
    snprintf(suffix, sizeof(suffix), " (HTTP %d)", result.providerStatus);
  }
  SendDrawHeaders(client, F("text/plain; charset=utf-8"),
                  strlen_P(reinterpret_cast<PGM_P>(result.message)) + strlen(suffix));
  client.print(result.message);
  client.print(suffix);
}

void SendDrawFrame(WiFiClient& client, const matrix::Rgb (&frame)[matrix::PixelCount]) {
  unsigned lit = 0;
  for (const auto& pixel : frame) {
    lit += pixel.r || pixel.g || pixel.b;
  }
  constexpr char prefix[] = "{\"rgb\":\"";
  char suffix[18];
  const int suffixLength = snprintf(suffix, sizeof(suffix), "\",\"lit\":%u}", lit);
  client.print(F("HTTP/1.1 200 OK\r\n"));
  SendDrawHeaders(client, F("application/json"), sizeof(prefix) - 1 + matrix::PixelCount * 6 + suffixLength);
  client.print(prefix);
  constexpr char hex[] = "0123456789abcdef";
  for (size_t y = 0; y < matrix::Height; ++y) {
    uint8_t row[matrix::Width * 6];
    size_t position = 0;
    for (size_t x = 0; x < matrix::Width; ++x) {
      const auto& pixel = frame[y * matrix::Width + x];
      for (const uint8_t channel : {pixel.r, pixel.g, pixel.b}) {
        row[position++] = hex[channel >> 4];
        row[position++] = hex[channel & 15];
      }
    }
    client.write(row, position);
  }
  client.print(suffix);
}

void HandleDrawing(WiFiClient& client, uint32_t started, std::string_view headers,
                   const matrix_http::RequestLine& request, FrameRenderer render) {
  if (wifi_setup::CurrentPhase() != wifi_setup::Phase::Online ||
      client.localIP() != wifi_setup::StationIp()) {
    SendStatus(client, Status::NotFound);
    return;
  }
  const bool config = request.path == "/api/config";
  const bool draw = request.path == "/api/draw";
  const bool post = request.method == "POST";
  if ((post && !config && !draw) || (!post && (draw || request.method != "GET"))) {
    client.print(F("HTTP/1.1 405 Method Not Allowed\r\nAllow: "));
    client.print(config ? F("GET, POST") : draw ? F("POST") : F("GET"));
    client.print(F("\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"));
    return;
  }
  size_t length = 0;
  const auto ip = client.localIP().toString();
  const auto status = draw_http::ParseHeaders(headers, {ip.c_str(), ip.length()}, post,
      config ? draw_http::MaxConfigBody : draw_http::MaxDrawBody, length);
  if (status != Status::Ready) {
    SendStatus(client, status);
    return;
  }
  if (!post) {
    if (config) {
      static constexpr char configured[] PROGMEM = "{\"configured\":true}";
      static constexpr char unconfigured[] PROGMEM = "{\"configured\":false}";
      const bool ready = model_api::IsConfigured();
      SendDrawAsset(client, F("application/json"), ready ? configured : unconfigured,
                    ready ? sizeof(configured) - 1 : sizeof(unconfigured) - 1);
    } else if (request.path == "/draw.js") {
      SendDrawAsset(client, F("text/javascript; charset=utf-8"), draw_http::DrawScript,
                    sizeof(draw_http::DrawScript) - 1);
    } else {
      SendDrawAsset(client, F("text/html; charset=utf-8"), draw_http::DrawPage,
                    sizeof(draw_http::DrawPage) - 1);
    }
    return;
  }

  std::unique_ptr<char[]> body(new (std::nothrow) char[length]);
  if (!body) {
    SendDrawError(client, {503, F("ESP не хватает памяти для запроса.")});
    return;
  }
  if (!ReadBody(client, started, reinterpret_cast<uint8_t*>(body.get()), length)) {
    return;
  }
  if (config) {
    const auto result = model_api::Configure(body.get(), length);
    if (result.status == 204) {
      client.print(F("HTTP/1.1 204 No Content\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n"));
    } else {
      SendDrawError(client, result);
    }
    return;
  }

  String prompt;
  const auto parsed = model_api::ParsePrompt(body.get(), length, prompt);
  body.reset();  // Do not keep the browser body in RAM during the TLS connection.
  if (parsed.status != 200) {
    SendDrawError(client, parsed);
    return;
  }
  static matrix::Rgb frame[matrix::PixelCount];  // One active HTTP request; keep it off the TLS call stack.
  const auto result = model_api::Draw(prompt, frame);
  if (result.status != 200) {
    SendDrawError(client, result);
    return;
  }
  render(frame);
  SendDrawFrame(client, frame);
}

void HandleWifi(WiFiClient& client, uint32_t started, std::string_view headers,
                const matrix_http::RequestLine& request) {
  const bool fromSetup = wifi_setup::IsSetupAddress(client.localIP());
  if (request.method != "GET" && request.method != "POST") {
    client.print(F("HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, POST\r\n"
                   "Connection: close\r\nContent-Length: 0\r\n\r\n"));
    return;
  }

  const bool form = request.method == "POST";
  if (form && !fromSetup) {
    client.print(F("HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"));
    return;
  }
  if (form && !wifi_setup::CanConfigure()) {
    client.print(F("HTTP/1.1 409 Conflict\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"));
    return;
  }

  size_t length = 0;
  if (fromSetup) {
    const auto status = wifi_http::ParseSetupHeaders(headers, form, length);
    if (status != Status::Ready) {
      SendStatus(client, status);
      return;
    }
  }
  if (!form) {
    SendWifiPage(client, fromSetup);
    return;
  }

  char body[wifi_http::MaxFormBytes];
  if (!ReadBody(client, started, reinterpret_cast<uint8_t*>(body), length)) {
    return;
  }
  wifi_http::Credentials credentials{};
  if (!wifi_http::ParseForm({body, length}, wifi_setup::FormToken(), credentials)) {
    SendWifiPage(client, true, F("Invalid form. Check the network name and password, then try again."));
    return;
  }
  if (wifi_setup::QueueConnection(credentials)) {
    RedirectToWifi(client);
  } else {
    client.print(F("HTTP/1.1 409 Conflict\r\nConnection: close\r\nContent-Length: 0\r\n\r\n"));
  }
}

void HandleClient(WiFiClient& client, FrameRenderer render) {
  const auto started = millis();
  const auto headers = ReadHeaders(client, started);
  if (headers.empty()) {
    return;
  }
  const auto request = matrix_http::ParseRequestLine(headers);
  if (request.path == "/wifi") {
    HandleWifi(client, started, headers, request);
    return;
  }
  if (request.path == "/" && request.method == "GET" && wifi_setup::IsSetupAddress(client.localIP())) {
    RedirectToWifi(client);
    return;
  }
  if (request.path == "/" || request.path == "/draw.js" ||
      request.path == "/api/config" || request.path == "/api/draw") {
    HandleDrawing(client, started, headers, request, render);
    return;
  }
  if (request.path == "/frame" &&
      (wifi_setup::CurrentPhase() != wifi_setup::Phase::Online ||
       client.localIP() != wifi_setup::StationIp())) {
    SendStatus(client, Status::NotFound);
    return;
  }
  const auto status = frame_http::ParseHeaders(headers);
  if (status != Status::Ready) {
    SendStatus(client, status);
    return;
  }

  matrix::Rgb frame[matrix::PixelCount];
  if (ReadBody(client, started, reinterpret_cast<uint8_t*>(frame), frame_http::FrameBytes)) {
    render(frame);
    SendStatus(client, Status::Ready);
  }
}

}  // namespace

void PollMatrixHttpServer(FrameRenderer render) {
  if (server.status() != LISTEN) {
    server.begin();
    if (server.status() != LISTEN) {
      return;
    }
  }
  auto client = server.accept();
  if (client) {
    client.setTimeout(500);
    HandleClient(client, render);
    client.stop(250);
  }
}
