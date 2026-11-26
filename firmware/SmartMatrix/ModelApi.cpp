#include "ModelApi.h"
#include "DrawHttpRequest.h"
#include "ModelFrame.h"
#include "ProviderRoots.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <time.h>
#include <utility>

namespace model_api {
namespace {

using namespace draw_http;

constexpr unsigned MaxResponse = 8192;
constexpr unsigned IoTimeoutMs = 30000;
constexpr time_t EarliestValidTime = 1704067200;  // 2024-01-01 UTC; reject the unset clock.
const char FrameInstruction[] PROGMEM =
  "Draw the user's request as a single 16x16 pixel-art image. "
  "Use only these case-sensitive palette symbols: . = black (unlit), W = white, R = red, "
  "O = orange, Y = yellow, G = green, C = cyan, B = blue, P = purple, M = magenta, "
  "S = light skin tone, N = brown, A = gray, D = dark gray. "
  "For a named character or object choose two to four distinctive features and signature colors. "
  "Draw them as visible blocks with strong contrast. Simplify or crop to a close-up if needed; "
  "never substitute an unrelated symbol. Use a fully black frame only when requested. "
  "Return only a JSON object with exactly one property, \"rows\": exactly 16 strings of 16 "
  "palette characters. Pixels run left to right, rows top to bottom. No spaces inside rows, "
  "no extra text or Markdown.";

struct Settings {
  bool tls = false;
  uint16_t port = 0;
  String host;
  String endpoint;
  String model;
  String key;
  String ca;
};

Settings settings;  // Volatile; never write provider data to flash or serial.

bool ReadText(JsonString text, std::size_t max, bool required, bool multiline, std::string_view& value) {
  if (text.isNull() || text.size() > max || (required && text.size() == 0)) {
    return false;
  }
  value = {text.c_str(), text.size()};
  bool visible = false;
  for (const unsigned char c : value) {
    if ((c < ' ' && (!multiline || (c != '\t' && c != '\n' && c != '\r'))) || c == 127) {
      return false;
    }
    visible |= c > ' ';
  }
  return !required || visible;
}

bool Append(String& out, std::string_view part) {
  return part.empty() || out.concat(part.data(), part.size());
}

class ResponseBody : public Print {
 public:
  explicit ResponseBody(String& output) : output_(output) {}

  int availableForWrite() override { return tooLarge || outOfMemory ? 0 : 512; }
  bool outputCanTimeout() override { return false; }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (length > MaxResponse - output_.length()) {
      tooLarge = true;
      return 0;
    }
    if (!output_.concat(reinterpret_cast<const char*>(data), length)) {
      outOfMemory = true;
      return 0;
    }
    return length;
  }

  bool tooLarge = false;
  bool outOfMemory = false;

 private:
  String& output_;
};

Result Fetch(WiFiClient& connection, String& payload, String& response,
             BearSSL::WiFiClientSecure* secure = nullptr) {
  HTTPClient api;
  if (!api.begin(connection, settings.host, settings.port, settings.endpoint, settings.tls)) {
    return {502, F("Не удалось открыть адрес провайдера.")};
  }
  api.setReuse(false);
  api.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  api.setTimeout(IoTimeoutMs);
  api.addHeader(F("Content-Type"), F("application/json"));
  api.addHeader(F("Accept"), F("application/json"));
  if (settings.key.length()) {
    String authorization(F("Bearer "));
    if (authorization.length() != 7 || !authorization.concat(settings.key)) {
      return {503, F("ESP не хватает памяти для запроса.")};
    }
    api.addHeader(F("Authorization"), authorization);
  }
  const char* headers[] = {"Content-Encoding"};
  api.collectHeaders(headers, 1);
  const int status = api.POST(reinterpret_cast<uint8_t*>(payload.begin()), payload.length());
  if (status < 0) {
    if (secure && secure->getLastSSLError() != 0) {
      return {502, F("Ошибка TLS: проверьте сертификат, имя хоста и поддержку TLS 1.2.")};
    }
    if (status == HTTPC_ERROR_READ_TIMEOUT) {
      return {504, F("Провайдер не ответил вовремя.")};
    }
    return {502, F("Нет соединения с провайдером. Проверьте адрес и сеть.")};
  }
  if (status != 200) {
    return {502, F("Провайдер отклонил запрос."), status};
  }
  const auto encoding = api.header("Content-Encoding");
  if (encoding.length() && !encoding.equalsIgnoreCase("identity")) {
    return {502, F("Сжатый ответ провайдера не поддерживается.")};
  }
  if (api.getSize() > static_cast<int>(MaxResponse)) {
    return {502, F("Ответ провайдера превышает 8192 байта.")};
  }
  if (api.getSize() > 0 && !response.reserve(api.getSize())) {
    return {503, F("ESP не хватает памяти для ответа.")};
  }
  ResponseBody body(response);
  const int read = api.writeToPrint(&body);
  if (body.tooLarge) {
    return {502, F("Ответ провайдера превышает 8192 байта.")};
  }
  if (body.outOfMemory) {
    return {503, F("ESP не хватает памяти для ответа.")};
  }
  if (read == HTTPC_ERROR_READ_TIMEOUT) {
    return {504, F("Провайдер не закончил ответ вовремя.")};
  }
  if (read <= 0 || read != static_cast<int>(response.length()) ||
      (api.getSize() >= 0 && read != api.getSize())) {
    return {502, F("Ответ провайдера пуст или получен не полностью.")};
  }
  return {200, nullptr};
}

Result RequestFrame(const String& prompt, String& response) {
  String payload;
  {
    JsonDocument request;
    request["model"] = settings.model;
    request["stream"] = false;
    auto messages = request["messages"].to<JsonArray>();
    auto system = messages.add<JsonObject>();
    system["role"] = "system";
    system["content"] = FPSTR(FrameInstruction);
    auto user = messages.add<JsonObject>();
    user["role"] = "user";
    user["content"] = prompt;
    if (request.overflowed() || !payload.reserve(measureJson(request)) || serializeJson(request, payload) == 0) {
      return {503, F("ESP не хватает памяти для запроса.")};
    }
  }

  if (!settings.tls) {
    WiFiClient plain;
    return Fetch(plain, payload, response);
  }

  if (time(nullptr) < EarliestValidTime) {
    return {503, F("ESP ещё не синхронизировал часы для HTTPS. Проверьте доступ к NTP и повторите.")};
  }
  // ESP8266's TLS parser reads through pgm_read_byte, which also accepts RAM.
  BearSSL::X509List roots(settings.ca.length() ? settings.ca.c_str() : DefaultRoots);
  if (roots.getCount() == 0) {
    return {503, F("Не удалось загрузить доверенные сертификаты.")};
  }
  BearSSL::WiFiClientSecure secure;
  secure.setSSLVersion(BR_TLS12, BR_TLS12);
  secure.setTrustAnchors(&roots);
  return Fetch(secure, payload, response, &secure);
}

void BeginTimeSync() {
  static bool started = false;
  if (!started) {
    started = true;
    configTime(0, 0, "pool.ntp.org", "time.google.com");
  }
}

}  // namespace

bool IsConfigured() {
  return settings.port != 0;
}

Result Configure(const char* body, unsigned int length) {
  JsonDocument json;
  if (deserializeJson(json, body, length, DeserializationOption::NestingLimit(2)) ||
      !json.is<JsonObject>() || json.size() < 2 || json.size() > 4) {
    return {400, F("Ожидаются адрес API, модель, необязательные ключ и CA (PEM).")};
  }
  bool hasKey = false;
  bool hasCa = false;
  for (const JsonPairConst field : json.as<JsonObjectConst>()) {
    const std::string_view name(field.key().c_str(), field.key().size());
    if (name == "apiKey") {
      hasKey = true;
    } else if (name == "ca") {
      hasCa = true;
    } else if (name != "baseUrl" && name != "model") {
      return {400, F("Неизвестное поле настроек.")};
    }
  }
  std::string_view baseUrl, model, key, ca;
  if (!ReadText(json["baseUrl"].as<JsonString>(), MaxBaseUrl, true, false, baseUrl) ||
      !ReadText(json["model"].as<JsonString>(), MaxModel, true, false, model) ||
      (hasKey && !ReadText(json["apiKey"].as<JsonString>(), MaxKey, false, false, key)) ||
      (hasCa && !ReadText(json["ca"].as<JsonString>(), MaxCa, false, true, ca))) {
    return {400, F("Неверные настройки или превышен допустимый размер поля.")};
  }
  for (const unsigned char c : key) {
    if (c <= ' ' || c > '~') {
      return {400, F("Ключ должен содержать только печатные ASCII-символы без пробелов.")};
    }
  }
  ProviderUrl url{};
  if (!ParseProviderUrl(baseUrl, url) || (!url.tls && !ca.empty())) {
    return {400, F("Укажите базовый HTTP(S)-адрес API без параметров. CA допускается только для HTTPS.")};
  }
  Settings next;
  if (!Append(next.host, url.host) || !Append(next.endpoint, url.path) ||
      !next.endpoint.concat(F("/chat/completions")) || !Append(next.model, model) ||
      !Append(next.key, key) || !Append(next.ca, ca)) {
    return {503, F("ESP не хватает памяти для настроек.")};
  }
  if (next.ca.length()) {
    BearSSL::X509List custom(next.ca.c_str());
    if (custom.getCount() == 0) {
      return {400, F("Не удалось прочитать сертификат CA в формате PEM.")};
    }
  }
  next.tls = url.tls;
  next.port = url.port;
  settings = std::move(next);
  if (settings.tls) {
    BeginTimeSync();
  }
  return {204, nullptr};
}

Result ParsePrompt(const char* body, unsigned int length, String& prompt) {
  JsonDocument json;
  std::string_view text;
  if (deserializeJson(json, body, length, DeserializationOption::NestingLimit(2)) ||
      !json.is<JsonObject>() || json.size() != 1 ||
      !ReadText(json["prompt"].as<JsonString>(), MaxPrompt, true, true, text)) {
    return {400, F("Введите промпт длиной до 768 байт UTF-8.")};
  }
  if (!Append(prompt, text)) {
    return {503, F("ESP не хватает памяти для промпта.")};
  }
  return {200, nullptr};
}

Result Draw(const String& prompt, matrix::Rgb (&frame)[matrix::PixelCount]) {
  if (!IsConfigured()) {
    return {409, F("Сначала сохраните настройки API.")};
  }
  String response;
  const auto result = RequestFrame(prompt, response);
  if (result.status != 200) {
    return result;
  }
  // Keep only the field we need; provider metadata and error bodies are never echoed.
  JsonDocument filter;
  filter["choices"][0]["message"]["content"] = true;
  JsonDocument json;
  if (filter.overflowed() ||
      deserializeJson(json, response, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(16))) {
    return {502, F("Провайдер не вернул ожидаемый JSON.")};
  }
  const auto content = json["choices"][0]["message"]["content"].as<JsonString>();
  if (content.isNull() || !ParseFrame({content.c_str(), content.size()}, frame)) {
    return {502, F("Модель не вернула корректный кадр 16×16.")};
  }
  return {200, nullptr};
}

}  // namespace model_api
