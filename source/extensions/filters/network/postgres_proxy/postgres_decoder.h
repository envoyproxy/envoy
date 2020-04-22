#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/postgres_proxy/postgres_session.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

// General callbacks for dispatching decoded Postgres messages to a sink.
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  virtual void incMessagesBackend() PURE;
  virtual void incMessagesFrontend() PURE;
  virtual void incMessagesUnknown() PURE;

  virtual void incSessionsEncrypted() PURE;
  virtual void incSessionsUnencrypted() PURE;

  enum class StatementType { Insert, Delete, Select, Update, Other, Noop };
  virtual void incStatements(StatementType) PURE;

  virtual void incTransactions() PURE;
  virtual void incTransactionsCommit() PURE;
  virtual void incTransactionsRollback() PURE;

  enum class NoticeType { Warning, Notice, Debug, Info, Log, Unknown };
  virtual void incNotices(NoticeType) PURE;

  enum class ErrorType { Error, Fatal, Panic, Unknown };
  virtual void incErrors(ErrorType) PURE;
};

// Postgres message decoder.
class Decoder {
public:
  virtual ~Decoder() = default;

  virtual bool onData(Buffer::Instance& data, bool frontend) PURE;
  virtual PostgresSession& getSession() PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl(DecoderCallbacks* callbacks) : callbacks_(callbacks) { initialize(); }

  bool onData(Buffer::Instance& data, bool frontend) override;
  PostgresSession& getSession() override { return session_; }

  void setMessage(std::string message) { message_ = message; }
  std::string getMessage() { return message_; }

  void setMessageLength(uint32_t message_len) { message_len_ = message_len; }
  uint32_t getMessageLength() { return message_len_; }

  void setStartup(bool startup) { startup_ = startup; }
  void initialize();

  bool encrypted() const { return encrypted_; }

protected:
  // Message action defines the Decoder's method which will be invoked
  // when a specific message has been decoded.
  using MsgAction = std::function<void(DecoderImpl*)>;

  // MsgProcessor has two fields:
  // first - string with message description
  // second - vector of Decoder's methods which are invoked when the message
  // is processed.
  using MsgProcessor = std::pair<std::string, std::vector<MsgAction>>;

  // Frontend and Backend messages.
  using MsgGroup = struct {
    // String describing direction (Frontend or Backend).
    std::string direction_;
    // Hash map indexed by messages' 1st byte points to handlers used for processing messages.
    absl::flat_hash_map<char, MsgProcessor> messages_;
    // Handler used for processing messages not found in hash map.
    MsgProcessor unknown_;
  };

  // Hash map binding keyword found in a message to an
  // action to be executed when the keyword is found.
  using KeywordProcessor = absl::flat_hash_map<std::string, MsgAction>;

  // Structure is used for grouping keywords found in a specific message.
  // Known keywords are dispatched via hash map and unknown keywords
  // are handled by unknown_.
  using MsgParserDict = struct {
    // Handler for known keywords.
    KeywordProcessor keywords_;
    // Handler invoked when a keyword is not found in hash map.
    MsgAction unknown_;
  };

  bool parseMessage(Buffer::Instance& data);
  void decode(Buffer::Instance& data);
  void decodeAuthentication();
  void decodeBackendStatements();
  void decodeBackendErrorResponse();
  void decodeBackendNoticeResponse();
  void decodeFrontendTerminate();
  void decodeErrorNotice(MsgParserDict& types);

  void incMessagesUnknown() { callbacks_->incMessagesUnknown(); }
  void incSessionsEncrypted() { callbacks_->incSessionsEncrypted(); }
  void incSessionsUnencrypted() { callbacks_->incSessionsUnencrypted(); }

  DecoderCallbacks* callbacks_{};
  PostgresSession session_{};

  // The following fields store result of message parsing.
  char command_{};
  std::string message_;
  uint32_t message_len_{};

  bool startup_{true};    // startup stage does not have 1st byte command
  bool encrypted_{false}; // tells if exchange is encrypted

  // Dispatchers for Backend (BE) and Frontend (FE) messages.
  MsgGroup FE_messages_;
  MsgGroup BE_messages_;

  // Handler for startup postgres message.
  // Startup message message which does not start with 1 byte TYPE.
  // It starts with message length and must be therefore handled
  // differently.
  MsgProcessor first_;

  // hash map for dispatching backend transaction messages
  KeywordProcessor BE_statements_;

  MsgParserDict BE_errors_;
  MsgParserDict BE_notices_;
};

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
