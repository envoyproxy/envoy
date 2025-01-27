#pragma once

#include <cstdint>
#include <string>

#include "envoy/stream_info/stream_info.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_command.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// Class stores data about the current state of a transaction between SMTP client and server.

class SmtpTransaction {
public:
  enum class State {
    None = 0,
    TransactionRequest = 1,
    TransactionInProgress = 2,
    TransactionAbortRequest = 3,
    TransactionAborted = 4,
    MailDataTransferRequest = 5,
    RcptCommand = 6,
    TransactionCompleted = 7,
    XReqIdTransfer = 8,
  };

  SmtpTransaction(std::string& session_id, DecoderCallbacks* callbacks, TimeSource& time_source,
                  Random::RandomGenerator& random_generator);

  std::string& getTransactionId() { return transaction_id_; }
  void setState(SmtpTransaction::State state) { state_ = state; }
  SmtpTransaction::State getState() { return state_; }

  void setStatus(const std::string status) { status_ = status; }
  const std::string& getStatus() const { return status_; }

  void setSender(std::string& sender) { sender_ = sender; }
  std::string& getSender() { return sender_; }

  void addRcpt(std::string& rcpt) { recipients_.push_back(rcpt); }

  uint8_t getNoOfRecipients() { return recipients_.size(); }
  // Adds number of bytes to mail data payload.
  void addPayloadBytes(uint64_t bytes) { payload_size_ += bytes; }
  void encode(ProtobufWkt::Struct& metadata);

  void addTrxnCommand(std::shared_ptr<SmtpCommand> command) { trxn_commands_.push_back(command); }
  void onComplete();
  void emitLog();
  void setXReqIdSent(bool status) { x_req_id_sent_ = status; }
  bool isXReqIdSent() { return x_req_id_sent_; }
  StreamInfo::StreamInfo& getStreamInfo() { return stream_info_; }

private:
  std::string transaction_id_;
  std::string session_id_;
  SmtpTransaction::State state_{State::None};
  // Transaction status
  std::string status_;
  std::string sender_;
  std::vector<std::string> recipients_;
  uint64_t payload_size_ = 0;
  std::vector<std::shared_ptr<SmtpCommand>> trxn_commands_;
  DecoderCallbacks* callbacks_{};
  TimeSource& time_source_;
  Random::RandomGenerator& random_generator_;
  StreamInfo::StreamInfoImpl stream_info_;
  bool x_req_id_sent_{false};
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
