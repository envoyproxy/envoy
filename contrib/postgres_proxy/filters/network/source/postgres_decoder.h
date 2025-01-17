#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "contrib/common/sqlutils/source/sqlutils.h"
#include "contrib/postgres_proxy/filters/network/source/postgres_message.h"
#include "contrib/postgres_proxy/filters/network/source/postgres_session.h"

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

  virtual void processQuery(const std::string&) PURE;

  virtual bool onSSLRequest() PURE;
  virtual bool shouldEncryptUpstream() const PURE;
  virtual void sendUpstream(Buffer::Instance&) PURE;
  virtual bool encryptUpstream(bool, Buffer::Instance&) PURE;
};

// Postgres message decoder.
class Decoder {
public:
  virtual ~Decoder() = default;

  // The following values are returned by the decoder, when filter
  // passes bytes of data via onData method:
  enum class Result {
    ReadyForNext, // Decoder processed previous message and is ready for the next message.
    NeedMoreData, // Decoder needs more data to reconstruct the message.
    Stopped // Received and processed message disrupts the current flow. Decoder stopped accepting
            // data. This happens when decoder wants filter to perform some action, for example to
            // call starttls transport socket to enable TLS.
  };
  virtual Result onData(Buffer::Instance& data, bool frontend) PURE;
  virtual PostgresSession& getSession() PURE;

  const Extensions::Common::SQLUtils::SQLUtils::DecoderAttributes& getAttributes() const {
    return attributes_;
  }

protected:
  // Decoder attributes extracted from Startup message.
  // It can be username, database name, client app type, etc.
  Extensions::Common::SQLUtils::SQLUtils::DecoderAttributes attributes_;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl(DecoderCallbacks* callbacks) : callbacks_(callbacks) { initialize(); }

  Result onData(Buffer::Instance& data, bool frontend) override;
  PostgresSession& getSession() override { return session_; }

  std::string getMessage() { return message_; }

  void initialize();

  bool encrypted() const { return encrypted_; }

  enum class State {
    InitState,
    InSyncState,
    OutOfSyncState,
    EncryptedState,
    NegotiatingUpstreamSSL
  };
  State state() const { return state_; }
  void state(State state) { state_ = state; }

protected:
  State state_{State::InitState};

  Result onDataInit(Buffer::Instance& data, bool frontend);
  Result onDataInSync(Buffer::Instance& data, bool frontend);
  Result onDataIgnore(Buffer::Instance& data, bool frontend);
  Result onDataInNegotiating(Buffer::Instance& data, bool frontend);

  // MsgAction defines the Decoder's method which will be invoked
  // when a specific message has been decoded.
  using MsgAction = std::function<void(DecoderImpl*)>;

  // MsgBodyReader is a function which returns a pointer to a Message
  // class which is able to read the Postgres message body.
  // The Postgres message body structure depends on the message type.
  using MsgBodyReader = std::function<std::unique_ptr<Message>()>;

  // MessageProcessor has the following fields:
  // first - string with message description
  // second - function which instantiates a Message object of specific type
  // which is capable of parsing the message's body.
  // third - vector of Decoder's methods which are invoked when the message
  // is processed.
  using MessageProcessor = std::tuple<std::string, MsgBodyReader, std::vector<MsgAction>>;

  // Frontend and Backend messages.
  using MsgGroup = struct {
    // String describing direction (Frontend or Backend).
    absl::string_view direction_;
    // Hash map indexed by messages' 1st byte points to handlers used for processing messages.
    absl::flat_hash_map<char, MessageProcessor> messages_;
    // Handler used for processing messages not found in hash map.
    MessageProcessor unknown_;
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

  void processMessageBody(Buffer::Instance& data, absl::string_view direction, uint32_t length,
                          MessageProcessor& msg, const std::unique_ptr<Message>& parser);
  void decode(Buffer::Instance& data);
  void decodeAuthentication();
  void decodeBackendStatements();
  void decodeBackendErrorResponse();
  void decodeBackendNoticeResponse();
  void decodeFrontendTerminate();
  void decodeErrorNotice(MsgParserDict& types);
  void onQuery();
  void onParse();
  void onStartup();

  void incMessagesUnknown() { callbacks_->incMessagesUnknown(); }
  void incSessionsEncrypted() { callbacks_->incSessionsEncrypted(); }
  void incSessionsUnencrypted() { callbacks_->incSessionsUnencrypted(); }

  // Helper method generating currently processed message in
  // displayable format.
  const std::string genDebugMessage(const std::unique_ptr<Message>& parser, Buffer::Instance& data,
                                    uint32_t message_len);

  DecoderCallbacks* callbacks_{};
  PostgresSession session_{};

  // The following fields store result of message parsing.
  char command_{'-'};
  std::string message_;
  uint32_t message_len_{};

  bool encrypted_{false}; // tells if exchange is encrypted

  // Dispatchers for Backend (BE) and Frontend (FE) messages.
  MsgGroup FE_messages_;
  MsgGroup BE_messages_;

  // Handler for startup postgres message.
  // Startup message message which does not start with 1 byte TYPE.
  // It starts with message length and must be therefore handled
  // differently.
  MessageProcessor first_;

  // hash map for dispatching backend transaction messages
  KeywordProcessor BE_statements_;

  MsgParserDict BE_errors_;
  MsgParserDict BE_notices_;

  // Buffer used to temporarily store a downstream postgres packet
  // while sending other packets. Currently used only when negotiating
  // upstream SSL.
  Buffer::OwnedImpl temp_storage_;

  // MAX_STARTUP_PACKET_LENGTH is defined in Postgres source code
  // as maximum size of initial packet.
  // https://github.com/postgres/postgres/search?q=MAX_STARTUP_PACKET_LENGTH&type=code
  static constexpr uint64_t MAX_STARTUP_PACKET_LENGTH = 10000;
};

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
