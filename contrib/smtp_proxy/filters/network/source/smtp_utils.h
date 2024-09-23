#pragma once
#include <array>
#include <string>

#include "absl/strings/string_view.h"

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
    NeedMoreData, // Decoder needs more data to reconstruct the message.
    ProtocolError,
    ResumeLastResponse
  };

  enum class SessionType { PlainText, Tls };
  //= {"HELO", "EHLO", "AUTH", "MAIL", "RCPT", "DATA", "QUIT", "RSET", "STARTTLS", "XREQID"};
  constexpr static size_t maxCommandLen = 1024;
  constexpr static size_t maxResponseLen = 1024;
  constexpr static absl::string_view CRLF = "\r\n";
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
  inline static const char* xReqIdCommand = "XREQID";
  inline static const char* syntaxErrorNoParamsAllowed =
      "501 Syntax error (no parameters allowed)\r\n";
  inline static const char* outOfOrderCommandResponse = "503 Bad sequence of commands\r\n";
  inline static const char* readyToStartTlsResponse = "220 2.0.0 Ready to start TLS\r\n";
  inline static const char* tlsHandshakeErrorResponse = "550 5.0.0 TLS Handshake error\r\n";
  inline static const char* tlsNotSupportedResponse = "502 TLS not supported\r\n";
  inline static const std::string tlsSessionActiveAlready = "Already running in TLS";
  inline static const char* mailboxUnavailableResponse =
      "450  Requested mail action not taken: mailbox unavailable\r\n";

  inline static const char* statusSuccess = "success";
  inline static const char* statusFailed = "failed";
  inline static const char* statusError = "error";
  inline static const char* statusAborted = "aborted";
  inline static const char* via_upstream = "via_upstream";
  inline static const char* local_5xx_error = "local_5xx_error";
  inline static const char* plaintext_trxn_not_allowed = "plaintext_trxn_not_allowed";
  inline static const char* missing_ehlo = "missing_ehlo_cmd";
  inline static const char* missing_mail_from_cmd = "missing_mail_from_cmd";
  inline static const char* missing_rcpt_cmd = "missing_rcpt_cmd";
  inline static const char* duplicate_mail_from = "duplicate_mail_from_cmd";
  inline static const char* duplicate_auth = "502_already_authenticated";
  inline static const char* duplicate_starttls = "502_already_encrypted";
  inline static const char* downstream_tls_error = "downstream_tls_handshake_error";
  inline static const char* upstream_tls_error = "upstream_tls_handshake_error";
  inline static const char* protocol_error = "protocol_error";
  inline static const char* passthroughMode = "passthrough_mode";
  inline static const char* terminatedByEnvoyMsg = "terminated_by_envoy_due_to_cx_close";
  inline static const char* connectionClose = "connection_close";
  inline static const char* trxnAbortedDueToSessionClose = "aborted_due_to_session_close";
  inline static const char* abortedDueToEhlo = "aborted_due_to_ehlo";
  inline static const char* abortedDueToRset = "aborted_due_to_rset";
  static std::string generateResponse(int code, EnhancedCode enhCode, std::string text);
  static std::string extractAddress(std::string& arg);
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
