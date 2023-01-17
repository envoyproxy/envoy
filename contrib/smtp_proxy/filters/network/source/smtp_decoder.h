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
  virtual void incSmtpSessionsTerminated() PURE;
  virtual void incTlsTerminatedSessions() PURE;
  virtual void incTlsTerminationErrors() PURE;
  virtual void incUpstreamTlsSuccess() PURE;
  virtual void incUpstreamTlsFailed() PURE;

  virtual void incSmtpAuthErrors() PURE;
  virtual void incMailDataTransferErrors() PURE;
  virtual void incMailRcptErrors() PURE;
  
  virtual bool downstreamStartTls(absl::string_view) PURE;
  virtual bool sendReplyDownstream(absl::string_view) PURE;
  virtual bool upstreamTlsRequired() const PURE;
  virtual bool upstreamStartTls() PURE;
  virtual void closeDownstreamConnection() PURE;
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
  Decoder::Result parseCommand(Buffer::Instance& data);
  Decoder::Result parseResponse(Buffer::Instance& data);
  void setSessionEncrypted(bool flag) { session_encrypted_ = flag; }
  bool isSessionEncrypted() const { return session_encrypted_; }
  void handleDownstreamTls();
  void decodeSmtpTransactionCommands(std::string&);
  void decodeSmtpTransactionResponse(uint16_t&);

  const char* SessionStates[9] = {
      "CONNECTION_REQUEST",       "CONNECTION_SUCCESS",          "SESSION_INIT_REQUEST",
      "SESSION_IN_PROGRESS",      "SESSION_TERMINATION_REQUEST", "SESSION_TERMINATED",
      "UPSTREAM_TLS_NEGOTIATION", "DOWNSTREAM_TLS_NEGOTIATION",  "SESSION_AUTH_REQUEST",
  };

  const char* TransactionStates[8] = {
      "NONE",
      "TRANSACTION_REQUEST",
      "TRANSACTION_IN_PROGRESS",
      "TRANSACTION_ABORT_REQUEST",
      "TRANSACTION_ABORTED",
      "MAIL_DATA_TRANSFER_REQUEST",
      "RCPT_COMMAND",
      "TRANSACTION_COMPLETED",
  };

protected:
 
  DecoderCallbacks* callbacks_{};
  SmtpSession session_;

  bool session_encrypted_{false}; // tells if exchange is encrypted

};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
