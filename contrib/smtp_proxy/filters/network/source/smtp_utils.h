#pragma once

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpUtils {
public:
  inline static const char* smtpHeloCommand = "HELO";
  inline static const char* smtpEhloCommand = "EHLO";
  inline static const char* smtpAuthCommand = "AUTH";
  inline static const char* smtpMailCommand = "MAIL";
  inline static const char* smtpRcptCommand = "RCPT";
  inline static const char* smtpDataCommand = "DATA";
  inline static const char* smtpQuitCommand = "QUIT";
  inline static const char* smtpRsetCommand = "RSET";
  inline static const char* startTlsCommand = "STARTTLS";
  inline static const char* syntaxErrorNoParamsAllowed =
      "501 Syntax error (no parameters allowed)\r\n";
  inline static const char* outOfOrderCommandResponse = "503 Bad sequence of commands\r\n";
  inline static const char* readyToStartTlsResponse = "220 Ready to start TLS\r\n";
  inline static const char* tlsHandshakeErrorResponse =
      "550 TLS Handshake Error\r\n";
  inline static const char* tlsNotSupportedResponse =
      "502 TLS not supported\r\n";
  inline static const char* mailboxUnavailableResponse =
      "450  Requested mail action not taken: mailbox unavailable\r\n";
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
