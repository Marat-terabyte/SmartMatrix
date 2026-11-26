#include "../../firmware/SmartMatrix/WifiHttpRequest.h"

using wifi_http::ParseSetupHeaders;
using wifi_http::Status;

constexpr bool AcceptHeaders(std::string_view headers, bool form, std::size_t expectedBytes) {
  std::size_t bytes = 0;
  return ParseSetupHeaders(headers, form, bytes) == Status::Ready && bytes == expectedBytes;
}

static_assert(AcceptHeaders("GET /wifi HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n", false, 0));
static_assert(!AcceptHeaders("GET /wifi HTTP/1.1\r\nHost: example.com\r\n\r\n", false, 0));
static_assert(AcceptHeaders("POST /wifi HTTP/1.1\r\nHost: 192.168.4.1:80\r\n"
                            "Content-Type: application/x-www-form-urlencoded\r\n"
                            "Content-Length: 384\r\n\r\n", true, 384));
static_assert(!AcceptHeaders("POST /wifi HTTP/1.1\r\nHost: 192.168.4.1\r\n"
                             "Content-Type: application/x-www-form-urlencoded\r\n"
                             "Content-Length: 385\r\n\r\n", true, 385));
static_assert(!AcceptHeaders("POST /wifi HTTP/1.1\r\nHost: 192.168.4.1\r\n"
                             "Content-Type: application/x-www-form-urlencoded\r\n"
                             "Content-Length: 7\r\nContent-Length: 7\r\n\r\n", true, 7));
static_assert(!AcceptHeaders("POST /wifi HTTP/1.1\r\nHost: 192.168.4.1\r\n"
                             "Content-Type: application/x-www-form-urlencoded\r\n"
                             "Content-Length: 7\r\nTransfer-Encoding: chunked\r\n\r\n", true, 7));
static_assert(!AcceptHeaders("POST /wifi HTTP/1.1\r\nHost: 192.168.4.1\r\n"
                             "Content-Type: text/plain\r\nContent-Length: 7\r\n\r\n", true, 7));

constexpr auto Token = "0123456789ABCDEF0123456789ABCDEF";

constexpr bool AcceptForm(std::string_view body, std::string_view ssid, std::string_view password) {
  wifi_http::Credentials credentials{};
  return wifi_http::ParseForm(body, Token, credentials) &&
         std::string_view(credentials.ssid) == ssid &&
         std::string_view(credentials.password) == password;
}

static_assert(AcceptForm("ssid=My+Wi-Fi%2B%D0%AF&password=%2Bhello%26%25&token=0123456789ABCDEF0123456789ABCDEF",
                         "My Wi-Fi+\xD0\xAF", "+hello&%"));
static_assert(AcceptForm("token=0123456789ABCDEF0123456789ABCDEF&ssid=Open&password=", "Open", ""));
static_assert(AcceptForm("ssid=12345678901234567890123456789012"
                         "&password=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
                         "&token=0123456789ABCDEF0123456789ABCDEF",
                         "12345678901234567890123456789012",
                         "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));

constexpr bool RejectForm(std::string_view body) {
  wifi_http::Credentials credentials{};
  return !wifi_http::ParseForm(body, Token, credentials);
}

static_assert(RejectForm("ssid=Home&password=longenough"));
static_assert(RejectForm("ssid=Home&password=longenough&token=wrong"));
static_assert(RejectForm("ssid=Home&password=&token=0123456789ABCDEF0123456789ABCDEF&ssid=Other"));
static_assert(RejectForm("ssid=Home&password=%00longenough&token=0123456789ABCDEF0123456789ABCDEF"));
static_assert(RejectForm("ssid=Home&password=bad%2&token=0123456789ABCDEF0123456789ABCDEF"));
static_assert(RejectForm("ssid=Home&password=short&token=0123456789ABCDEF0123456789ABCDEF"));
static_assert(RejectForm("ssid=&password=&token=0123456789ABCDEF0123456789ABCDEF"));
static_assert(RejectForm("ssid=123456789012345678901234567890123&password=&token=0123456789ABCDEF0123456789ABCDEF"));
static_assert(RejectForm("ssid=Home&password=&token=0123456789ABCDEF0123456789ABCDEF&"));
