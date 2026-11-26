#include "ModelApi.h"
#include "DrawHttpRequest.h"
#include "ModelFrame.h"
#include "ProviderBody.h"
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
  bool insecureTls = false;
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

class ProviderInput {
 public:
  explicit ProviderInput(WiFiClient& stream) : stream_(stream), started_(millis()) {}

  int read() {
    if (offset_ < length_) {
      return bytes_[offset_++];
    }
    while (millis() - started_ < IoTimeoutMs) {
      const int available = stream_.available();
      if (available > 0) {
        const auto size = static_cast<size_t>(available);
        const int count = stream_.read(bytes_, size < sizeof(bytes_) ? size : sizeof(bytes_));
        if (count > 0) {
          length_ = count;
          offset_ = 1;
          return bytes_[0];
        }
      }
      if (!stream_.connected()) {
        return provider_body::End;
      }
      delay(1);
    }
    return provider_body::Timeout;
  }

 private:
  WiFiClient& stream_;
  uint32_t started_;
  size_t length_ = 0;
  size_t offset_ = 0;
  uint8_t bytes_[128];
};

Result ReadProviderFrame(HTTPClient& api, matrix::Rgb (&frame)[matrix::PixelCount]) {
  using provider_body::Framing;
  using provider_body::State;

  const auto encoding = api.header("Content-Encoding");
  if (encoding.length() && !encoding.equalsIgnoreCase("identity")) {
    return {502, F("Сжатый ответ провайдера не поддерживается.")};
  }
  const auto transfer = api.header("Transfer-Encoding");
  if (transfer.length() && !transfer.equalsIgnoreCase("chunked")) {
    return {502, F("Формат передачи ответа провайдера не поддерживается.")};
  }
  const int size = api.getSize();
  const auto framing = transfer.length() ? Framing::Chunked :
                       size >= 0 ? Framing::FixedLength : Framing::UntilClose;
  if (framing == Framing::FixedLength && size > static_cast<int>(MaxResponse)) {
    return {502, F("Ответ провайдера превышает 8192 байта.")};
  }
  auto* stream = api.getStreamPtr();
  if (!stream) {
    return {502, F("Ответ провайдера пуст или получен не полностью.")};
  }

  ProviderInput input(*stream);
  provider_body::Reader body(input, framing, size >= 0 ? size : 0, MaxResponse);
  JsonDocument filter;
  filter["choices"][0]["message"]["content"] = true;
  if (filter.overflowed()) {
    return {503, F("ESP не хватает памяти для ответа.")};
  }
  JsonDocument json;
  const auto error = deserializeJson(json, body, DeserializationOption::Filter(filter),
                                     DeserializationOption::NestingLimit(16));
  if (!error) {
    body.FinishJson();
  }
  switch (body.state()) {
    case State::TooLarge: return {502, F("Ответ провайдера превышает 8192 байта.")};
    case State::TimedOut: return {504, F("Провайдер не закончил ответ вовремя.")};
    case State::Invalid: return {502, F("Ответ провайдера оборван или содержит лишние данные.")};
    case State::Reading:
    case State::Complete: break;
  }
  if (error == DeserializationError::NoMemory) {
    return {503, F("ESP не хватает памяти для ответа.")};
  }
  if (error) {
    return {502, F("Провайдер не вернул ожидаемый JSON.")};
  }
  const auto content = json["choices"][0]["message"]["content"].as<JsonString>();
  if (content.isNull() || !ParseFrame({content.c_str(), content.size()}, frame)) {
    return {502, F("Модель не вернула корректный кадр 16×16.")};
  }
  return {200, nullptr};
}

Result Fetch(WiFiClient& connection, String& payload, matrix::Rgb (&frame)[matrix::PixelCount],
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
  const char* headers[] = {"Content-Encoding", "Transfer-Encoding"};
  api.collectHeaders(headers, sizeof(headers) / sizeof(headers[0]));
  const int status = api.POST(reinterpret_cast<uint8_t*>(payload.begin()), payload.length());
  payload = String();  // The JSON request is no longer needed while reading the response.
  if (status < 0) {
    if (secure && secure->getLastSSLError() != 0) {
      if (settings.insecureTls) {
        return {502, F("Не удалось установить TLS. Проверьте адрес и поддержку TLS 1.2.")};
      }
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
  return ReadProviderFrame(api, frame);
}

Result RequestFrame(String& prompt, matrix::Rgb (&frame)[matrix::PixelCount]) {
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
  prompt = String();  // The prompt is now present in the serialized request.

  if (!settings.tls) {
    WiFiClient plain;
    return Fetch(plain, payload, frame);
  }

  BearSSL::X509List roots;
  if (!settings.insecureTls) {
    if (time(nullptr) < EarliestValidTime) {
      return {503, F("ESP ещё не синхронизировал часы для HTTPS. Проверьте доступ к NTP и повторите.")};
    }
    // ESP8266's TLS parser reads through pgm_read_byte, which also accepts RAM.
    if (!roots.append(settings.ca.length() ? settings.ca.c_str() : DefaultRoots) || roots.getCount() == 0) {
      return {503, F("Не удалось загрузить доверенные сертификаты.")};
    }
  }
  BearSSL::WiFiClientSecure secure;
  secure.setSSLVersion(BR_TLS12, BR_TLS12);
  if (settings.insecureTls) {
    secure.setInsecure();
  } else {
    secure.setTrustAnchors(&roots);
  }
  return Fetch(secure, payload, frame, &secure);
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
      !json.is<JsonObject>() || json.size() < 2 || json.size() > 5) {
    return {400, F("Ожидаются адрес API и модель; ключ и параметры TLS необязательны.")};
  }
  bool hasKey = false;
  bool hasCa = false;
  bool insecureTls = false;
  for (const JsonPairConst field : json.as<JsonObjectConst>()) {
    const std::string_view name(field.key().c_str(), field.key().size());
    if (name == "apiKey") {
      hasKey = true;
    } else if (name == "ca") {
      hasCa = true;
    } else if (name == "insecureTls") {
      if (!field.value().is<bool>()) {
        return {400, F("insecureTls должен быть логическим значением.")};
      }
      insecureTls = field.value().as<bool>();
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
  if (!ParseProviderUrl(baseUrl, url)) {
    return {400, F("Укажите базовый HTTP(S)-адрес API без параметров.")};
  }
  if (!url.tls && (insecureTls || !ca.empty())) {
    return {400, F("Параметры сертификата доступны только для HTTPS.")};
  }
  if (insecureTls && !ca.empty()) {
    return {400, F("Не указывайте CA, если проверка сертификата отключена.")};
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
  next.insecureTls = insecureTls;
  next.port = url.port;
  settings = std::move(next);
  if (settings.tls && !settings.insecureTls) {
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

Result Draw(String prompt, matrix::Rgb (&frame)[matrix::PixelCount]) {
  if (!IsConfigured()) {
    return {409, F("Сначала сохраните настройки API.")};
  }
  return RequestFrame(prompt, frame);
}

}  // namespace model_api
