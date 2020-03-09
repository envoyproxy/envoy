#include "extensions/filters/network/postgresql_proxy/postgresql_decoder.h"
#include "extensions/filters/network/postgresql_proxy/postgresql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

void DecoderImpl::parseMessage(Buffer::Instance& data) {
  ENVOY_LOG(trace, "postgresql_proxy: parsing message, len {}", data.length());

  std::string cmd;
  std::string message;
  uint32_t length;

  setMessageLength(data.length());

  BufferHelper::readStringBySize(data, 1, cmd);
  setCommand(cmd);

  BufferHelper::readUint32(data, length); // just drain the four bytes

  BufferHelper::readStringBySize(data, data.length(), message);
  setMessage(message);

  ENVOY_LOG(trace, "postgresql_proxy: msg parsed");
}

void DecoderImpl::decode(Buffer::Instance& data) {
  ENVOY_LOG(trace, "postgresql_proxy: decoding {} bytes", data.length());

  parseMessage(data);

  if (isBackend()) {
    decodeBackend();
  } else {
    decodeFrontend();
  }

  ENVOY_LOG(trace, "postgresql_proxy: {} bytes remaining in buffer", data.length());
}

void DecoderImpl::decodeBackend() {
  ENVOY_LOG(debug, "(Backend) command = {}", getCommand());
  ENVOY_LOG(debug, "(Backend) length = {}", getMessageLength());
  ENVOY_LOG(debug, "(Backend) message = {}", getMessage());

  // Decode the backend commands
  switch (getCommand()[0])
  {
  case 'C': // CommandComplete
  case '2': // BindComplete
  case 'S': // ParameterStatus
    decodeBackendStatements();
    break;

  case '1': // ParseComplete
    callbacks_.incStatements();
    callbacks_.incStatementsOther();
    break;

  case 'T': // RowDescription
    decodeBackendRowDescription();
    break;

  case 'R': // AuthenticationOk
    callbacks_.incSessions();
    break;

  case 'E': // ErrorResponse
    decodeBackendErrorResponse();
    break;

  case 'N': // NoticeResponse
    decodeBackendNoticeResponse();
    break;
  }
}

void DecoderImpl::decodeBackendStatements() {
  callbacks_.incStatements();
  if (getMessage().find("BEGIN") != std::string::npos) {
    callbacks_.incStatementsOther();
    session_.setInTransaction(true);
  } else if (getMessage().find("START TRANSACTION") != std::string::npos) {
    callbacks_.incStatementsOther();
    session_.setInTransaction(true);
  } else if (getMessage().find("ROLLBACK") != std::string::npos) {
    callbacks_.incStatementsOther();
    session_.setInTransaction(false);
    callbacks_.incTransactionsRollback();
  } else if (getMessage().find("COMMIT") != std::string::npos) {
    callbacks_.incStatementsOther();
    session_.setInTransaction(false);
    callbacks_.incTransactionsCommit();
  } else if (getMessage().find("INSERT") != std::string::npos) {
    callbacks_.incStatementsInsert();
    callbacks_.incTransactionsCommit();
  } else if (getMessage().find("UPDATE") != std::string::npos) {
    callbacks_.incStatementsUpdate();
    callbacks_.incTransactionsCommit();
  } else if (getMessage().find("DELETE") != std::string::npos) {
    callbacks_.incStatementsDelete();
    callbacks_.incTransactionsCommit();
  } else {
    callbacks_.incStatementsOther();
    callbacks_.incTransactionsCommit();
  }
}

void DecoderImpl::decodeBackendErrorResponse() {
  if (getMessage().find("VERROR") != std::string::npos) {
    callbacks_.incErrors();
  }
}

void DecoderImpl::decodeBackendNoticeResponse() {
  if (getMessage().find("VWARNING") != std::string::npos) {
    callbacks_.incWarnings();
  }
}

void DecoderImpl::decodeBackendRowDescription() {
  callbacks_.incStatements();
  callbacks_.incStatementsSelect();
  callbacks_.incTransactionsCommit();
}

void DecoderImpl::decodeFrontend() {
  ENVOY_LOG(debug, "(Frontend) command = {}", getCommand());
  ENVOY_LOG(debug, "(Frontend) length = {}", getMessageLength());
  ENVOY_LOG(debug, "(Frontend) message = {}", getMessage());

  switch (getCommand()[0])
  {
  case 'Q':
    session_.setProtocolType(PostgreSQLSession::ProtocolType::Simple);
    break;

  case 'P':
  case 'B':
    session_.setProtocolType(PostgreSQLSession::ProtocolType::Extended);
    break;
  }
}

bool DecoderImpl::isBackend() {
  return (session_.getProtocolDirection() == PostgreSQLSession::ProtocolDirection::Backend);
}

bool DecoderImpl::isFrontend() {
  return (session_.getProtocolDirection() == PostgreSQLSession::ProtocolDirection::Frontend);
}

void DecoderImpl::onData(Buffer::Instance& data) {
  decode(data);
}

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
