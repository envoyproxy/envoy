#pragma once

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpTestUtils {
public:
  inline static const char* smtpHeloCommand = "HELO test.com\r\n";
  inline static const char* smtpEhloCommand = "EHLO test.com\r\n";
  inline static const char* smtpAuthCommand = "AUTH PLAIN AHVzZXJuYW1lAHBhc3N3b3Jk";
  inline static const char* smtpMailCommand = "MAIL FROM:<test@example.com>\r\n";
  inline static const char* smtpRcptCommand = "RCPT TO:<test@example.com>\r\n";
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
