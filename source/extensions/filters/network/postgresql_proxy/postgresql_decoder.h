#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

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

/**
 * PostgreSQL message decoder.
 */
class Decoder {
public:
  virtual ~Decoder() = default;

  virtual void onData(Buffer::Instance& data) PURE;
  virtual PostgreSQLSession& getSession() PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // PostgreSQLProxy::Decoder
  void onData(Buffer::Instance& data) override;
  PostgreSQLSession& getSession() override { return session_; }

  // Temp stuff for testing purposes
  void setCommand(std::string command) { command_ = command; }
  std::string getCommand() { return command_; }

  void setMessage(std::string message) { message_ = message; }
  std::string getMessage() { return message_; }
  
  void setMessageLength(uint32_t message_len) { message_len_ = message_len; }
  uint32_t getMessageLength() { return message_len_; }
private:
  void parseMessage(Buffer::Instance& data);
  void decode(Buffer::Instance& data);
  void decodeBackend();
  void decodeBackendStatements();
  void decodeBackendErrorResponse();
  void decodeBackendNoticeResponse();
  void decodeBackendRowDescription();

  void decodeFrontend();
  bool isFrontend();
  bool isBackend();

  DecoderCallbacks& callbacks_;
  PostgreSQLSession session_;

  std::string command_;
  std::string message_;
  uint32_t message_len_;
  bool in_transaction_{false};
};

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
