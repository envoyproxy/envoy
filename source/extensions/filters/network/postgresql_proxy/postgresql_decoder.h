#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/postgresql_proxy/postgresql_session.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

// General callbacks for dispatching decoded PostgreSQL messages to a sink.
class DecoderCallbacks {
public:
  enum NoticeType { Warning, Notice, Debug, Info, Log, Unknown };

  virtual ~DecoderCallbacks() = default;

  virtual void incBackend() PURE;

  virtual void incErrorsError() PURE;
  virtual void incErrorsFatal() PURE;
  virtual void incErrorsPanic() PURE;
  virtual void incErrorsUnknown() PURE;

  virtual void incFrontend() PURE;

  virtual void incSessionsEncrypted() PURE;
  virtual void incSessionsUnencrypted() PURE;

  virtual void incStatements() PURE;
  virtual void incStatementsDelete() PURE;
  virtual void incStatementsInsert() PURE;
  virtual void incStatementsOther() PURE;
  virtual void incStatementsSelect() PURE;
  virtual void incStatementsUpdate() PURE;

  virtual void incTransactions() PURE;
  virtual void incTransactionsCommit() PURE;
  virtual void incTransactionsRollback() PURE;

  virtual void incUnknown() PURE;
  virtual void incNotice(NoticeType) PURE;
};

// PostgreSQL message decoder.
class Decoder {
public:
  virtual ~Decoder() = default;

  virtual bool onData(Buffer::Instance& data, bool frontend) PURE;
  virtual PostgreSQLSession& getSession() PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl(DecoderCallbacks* callbacks) : callbacks_(callbacks) { initialize(); }

  bool onData(Buffer::Instance& data, bool frontend) override;
  PostgreSQLSession& getSession() override { return session_; }

  void setMessage(std::string message) { message_ = message; }
  std::string getMessage() { return message_; }

  void setMessageLength(uint32_t message_len) { message_len_ = message_len; }
  uint32_t getMessageLength() { return message_len_; }

  void setStartup(bool startup) { startup_ = startup; }
  void initialize();

  bool encrypted() const { return encrypted_; }

protected:
  bool parseMessage(Buffer::Instance& data);
  void decode(Buffer::Instance& data);
  void decodeAuthentication();
  void decodeBackendStatements();
  void decodeBackendErrorResponse();
  void decodeBackendNoticeResponse();
  void decodeFrontendTerminate();

  void incBackend() { callbacks_->incBackend(); }
  void incFrontend() { callbacks_->incFrontend(); }
  void incSessionsEncrypted() { callbacks_->incSessionsEncrypted(); }
  void incSessionsUnencrypted() { callbacks_->incSessionsUnencrypted(); }
  void incStatements() { callbacks_->incStatements(); }
  void incStatementsOther() { callbacks_->incStatementsOther(); }
  void incUnknown() { callbacks_->incUnknown(); }

  DecoderCallbacks* callbacks_;
  PostgreSQLSession session_;

  // the following fields store result of message parsing
  char command_;
  std::string message_;
  uint32_t message_len_;

  bool startup_{true};    // startup stage does not have 1st byte command
  bool encrypted_{false}; // tells is exchange is encrypted

  // Message action defines the Decoder's method which will be invoked
  // when a specific message has been decoded.
  using MsgAction = std::function<void(DecoderImpl*)>;

  // Message has two fields:
  // field 0 - string with message description
  // field 1 - vector of Decoder's methods which are invoked when the message
  // is processed.
  using Message = std::tuple<std::string, std::vector<MsgAction>>;

  // Frontend and Backend messages
  // Class could be used to group those values, but tuple is used until
  // functionality requires a switch.
  // field 0 - string describing direction (Frontend or Backend)
  // field 1 - hash map indexed by messages'1 1st byte points to data used for processing messages
  // field 2 - data used for processing messages not found in hash map
  std::tuple<std::string, absl::flat_hash_map<char, Message>, Message> FE_messages_;
  std::tuple<std::string, absl::flat_hash_map<char, Message>, Message> BE_messages_;

  // Handler for startup postgresql message.
  // Startup message message which does not start with 1 byte TYPE.
  // It starts with message length and must be therefore handled
  // differently.
  Message first_;

  // hash map for dispatching backend transaction messages
  absl::flat_hash_map<std::string, MsgAction> BE_statements_;

  // hash map for dispatching backend errors and notice messages
  absl::flat_hash_map<std::string, MsgAction> BE_errors_;
  absl::flat_hash_map<std::string, MsgAction> BE_notices_;
};

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
