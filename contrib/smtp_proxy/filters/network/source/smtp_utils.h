#pragma once
#include <array>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpUtils {
public:
  typedef std::array<int, 3> EnhancedCode;
  static constexpr EnhancedCode EnhancedCodeNotSet = {0, 0, 0};
  // The following values are returned by the decoder, when filter
  // passes bytes of data via onData/onWrite method:
  enum class Result {
    ReadyForNext, // Decoder processed previous message and is ready for the next message.
    Stopped, // Received and processed message disrupts the current flow. Decoder stopped accepting
             // data. This happens when decoder wants filter to perform some action, for example to
             // call starttls transport socket to enable TLS.
    ResumeLastResponse
  };

  enum class SessionType { PlainText, Tls };

  inline static const char* smtpCrlfSuffix = "\r\n";
  inline static const char* smtpHeloCommand = "HELO";
  inline static const char* smtpEhloCommand = "EHLO";
  inline static const char* smtpAuthCommand = "AUTH";
  inline static const char* smtpMailCommand = "MAIL";
  inline static const char* smtpRcptCommand = "RCPT";
  inline static const char* smtpDataCommand = "DATA";
  inline static const char* smtpQuitCommand = "QUIT";
  inline static const char* smtpRsetCommand = "RSET";
  inline static const char* startTlsCommand = "STARTTLS";
  inline static const char* xReqIdCommand = "X-REQUEST-ID";
  inline static const char* ehloFirstMsg = "Please introduce yourself first";
  inline static const char* syntaxErrorNoParamsAllowed =
      "501 Syntax error (no parameters allowed)\r\n";
  inline static const char* outOfOrderCommandResponse = "503 Bad sequence of commands\r\n";
  inline static const char* readyToStartTlsResponse = "220 2.0.0 Ready to start TLS\r\n";
  inline static const char* tlsHandshakeErrorResponse = "550 5.0.0 TLS Handshake error\r\n";
  inline static const char* tlsNotSupportedResponse = "502 TLS not supported\r\n";
  inline static const char* mailboxUnavailableResponse =
      "450  Requested mail action not taken: mailbox unavailable\r\n";

  inline static const char* statusSuccess = "Success";
  inline static const char* statusFailed = "Failed";
  inline static const char* statusAborted = "Aborted";
  static std::string generateResponse(int code, EnhancedCode enhCode, std::string text);
  static std::string extractAddress(std::string& arg);
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
