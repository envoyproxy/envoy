#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_session.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// General callbacks for dispatching decoded SMTP messages to a sink.
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  virtual void incSmtpTransactions() PURE;
  virtual void incSmtpTransactionsAborted() PURE;
  virtual void incSmtpSessionRequests() PURE;
  virtual void incSmtpConnectionEstablishmentErrors() PURE;
  virtual void incSmtpSessionsCompleted() PURE;
  virtual void incTlsTerminatedSessions() PURE;
  virtual void incSmtp4xxErrors() PURE;
  virtual void incSmtp5xxErrors() PURE;
  virtual bool onStartTlsCommand(absl::string_view) PURE;
  virtual bool sendReplyDownstream(absl::string_view) PURE;
};

// SMTP message decoder.
class Decoder {
public:
  virtual ~Decoder() = default;

  // The following values are returned by the decoder, when filter
  // passes bytes of data via onData method:
  enum class Result {
    ReadyForNext, // Decoder processed previous message and is ready for the next message.
    Stopped // Received and processed message disrupts the current flow. Decoder stopped accepting
            // data. This happens when decoder wants filter to perform some action, for example to
            // call starttls transport socket to enable TLS.
  };

  virtual Result onData(Buffer::Instance& data, bool) PURE;
  // virtual Result parseResponse(Buffer::Instance& data) PURE;
  virtual SmtpSession& getSession() PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl(DecoderCallbacks* callbacks) : callbacks_(callbacks) {}

  Result onData(Buffer::Instance& data, bool upstream) override;
  SmtpSession& getSession() override { return session_; }

  bool isSessionEncrypted() const { return session_encrypted_; }

protected:
  Decoder::Result parseCommand(Buffer::Instance& data);
  Decoder::Result parseResponse(Buffer::Instance& data);
  void parseMessage(Buffer::Instance& message, uint8_t seq, uint32_t len);

  DecoderCallbacks* callbacks_{};
  SmtpSession session_{};

  bool session_encrypted_{false}; // tells if exchange is encrypted
  inline static const char* smtpHeloCommand = "HELO";
  inline static const char* smtpEhloCommand = "EHLO";
  inline static const char* smtpMailCommand = "MAIL";
  inline static const char* smtpDataCommand = "DATA";
  inline static const char* smtpQuitCommand = "QUIT";
  inline static const char* smtpRsetCommand = "RSET";
  inline static const char* startTlsCommand = "STARTTLS";
  inline static const char* outOfOrderCommandResponse = "503 Bad sequence of commands\r\n";
  inline static const char* readyToStartTlsResponse = "220 Ready to start TLS\r\n";
  inline static const char* failedToStartTlsResponse =
      "454 TLS not available due to temporary reason\r\n";
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
