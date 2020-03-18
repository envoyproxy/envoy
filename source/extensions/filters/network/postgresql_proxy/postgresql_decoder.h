#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"
#include "absl/container/flat_hash_map.h"

#include "extensions/filters/network/postgresql_proxy/postgresql_session.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

/**
 * General callbacks for dispatching decoded PostgreSQL messages to a sink.
 */
class DecoderCallbacks {
public:
  virtual ~DecoderCallbacks() = default;

  virtual void incFrontend() PURE;
  virtual void incUnrecognized() PURE;

  virtual void incErrors() PURE;
  virtual void incSessions() PURE;
  virtual void incStatements() PURE;
  virtual void incStatementsDelete() PURE;
  virtual void incStatementsInsert() PURE;
  virtual void incStatementsOther() PURE;
  virtual void incStatementsSelect() PURE;
  virtual void incStatementsUpdate() PURE;
  virtual void incTransactions() PURE;
  virtual void incTransactionsCommit() PURE;
  virtual void incTransactionsRollback() PURE;
  virtual void incWarnings() PURE;
};

class DecoderImpl;

  using MsgAction = std::function<void(DecoderImpl*)>;
class MessageImpl {
public:
  //using MsgAction = std::add_pointer_t<void(DecoderImpl*)>;
  //using MsgAction = void(DecoderImpl*);

  MessageImpl(std::string descr, std::string type) : descr_(descr), type_(type) {}
  std::string getDescr() const {return descr_;}
  std::string getType() const {return type_;}
  void addAction(MsgAction action) {actions_.push_back(action);}
  const std::vector<MsgAction>& getActions() const { return actions_; }

private:
  std::string descr_;
  std::string type_;
  std::vector<MsgAction> actions_;
};

using Message = std::tuple<std::string, std::string, std::vector<MsgAction>>;

/**
 * PostgreSQL message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() = default;
  
  virtual bool onData(Buffer::Instance& data, bool frontend) PURE;
  virtual PostgreSQLSession& getSession() PURE;
};


using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
  //friend  MessageImpl;
public:
  DecoderImpl(DecoderCallbacks* callbacks) : callbacks_(callbacks) {initialize();}

  // PostgreSQLProxy::Decoder
  bool onData(Buffer::Instance& data, bool frontend) override;
  PostgreSQLSession& getSession() override { return session_; }

  void setMessage(std::string message) { message_ = message; }
  std::string getMessage() { return message_; }
  
  void setMessageLength(uint32_t message_len) { message_len_ = message_len; }
  uint32_t getMessageLength() { return message_len_; }

  void setInitial(bool initial) { initial_ = initial; }
  void initialize();
protected:
  bool parseMessage(Buffer::Instance& data);
  void decode(Buffer::Instance& data);
  void decodeBackendStatements();
  void decodeBackendErrorResponse();
  void decodeBackendNoticeResponse();
  void decodeBackendRowDescription();
  void incFrontend();
  void incUnrecognized();
  void incStatements();
  void incStatementsOther();
  void incSessions();
  void onAuthentication();

  DecoderCallbacks* callbacks_;
  PostgreSQLSession session_;

  // the following fields store result of message parsing
  char command_;
  std::string message_;
  uint32_t message_len_;


  bool in_transaction_{false};
  bool initial_{true}; // initial stage does not have 1st byte command

  using Message = std::tuple<std::string, std::vector<MsgAction>>;
  // Handler for initial postgresql message. 
  // Initial message is the only message which does not start with
  // 1 byte TYPE. It starts with message length and must be 
  // therefore handled differently.
  Message first_;

  // Frontend mnd Backend essages
  // Class could be used to group those values, but tuple is used until
  // functionality requires a switch.
  // field 0 - string describing direction (Frontend or Backend)
  // field 1 - hash map indexed by messages'1 1st byte points to data used for processing messages
  // field 2 - data used for processing messages not found in hash map
  std::tuple<std::string, absl::flat_hash_map<char, Message>, Message> FEmessages_; 
  std::tuple<std::string, absl::flat_hash_map<char, Message>, Message> BEmessages_; 
};

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
