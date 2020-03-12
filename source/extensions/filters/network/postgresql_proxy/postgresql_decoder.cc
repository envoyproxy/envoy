#include "extensions/filters/network/postgresql_proxy/postgresql_decoder.h"
#include "extensions/filters/network/postgresql_proxy/postgresql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

bool DecoderImpl::parseMessage(Buffer::Instance& data) {
  ENVOY_LOG(trace, "postgresql_proxy: parsing message, len {}", data.length());

  // The minimum size of the message sufficient for parsing is 5 bytes.
  if (data.length() < 5) {
    // not enough data in the buffer
    return false;
  }

  if (!initial_) {
  char com;
  data.copyOut(0, 1, &com);
  ENVOY_LOG(trace, "postgresql_proxy: command is {}", com);
  }

  // The 1 byte message type and message length should be in the buffer
  // Check if the entire message has been read.
  std::string cmd;
  std::string message;
  uint32_t length;
  data.copyOut(initial_ ? 0 : 1, 4, &length);
  length = ntohl(length);
  if (data.length() < (length + (initial_ ? 0 : 1))) {
    ENVOY_LOG(trace, "postgresql_proxy: cannot parse message. Need {} bytes in buffer", length + (initial_ ? 0 : 1));
    // not enough data in the buffer
    return false;
  }

  initial_ = false;

  setMessageLength(length);

  BufferHelper::readStringBySize(data, 1, cmd);
  command_ = cmd[0];
  //setCommand(cmd);

  data.drain(4); // this is length which we already know.

  BufferHelper::readStringBySize(data, length - 4, message);
  setMessage(message);

  ENVOY_LOG(trace, "postgresql_proxy: msg parsed");
  return true;
}

void DecoderImpl::decode(Buffer::Instance& data) {
  ENVOY_LOG(trace, "postgresql_proxy: decoding {} bytes", data.length());

  if(!parseMessage(data)) {
    return;
  }

  std::string command_type = "(Backend)";

  switch (command_)
  {
  case 'Q':
  case 'P':
  case 'B':
    command_type = "(Frontend)";
    callbacks_->incFrontend();
    break;

  case 'C': // CommandComplete
  case '2': // BindComplete
  case 'S': // ParameterStatus
    decodeBackendStatements();
    break;

  case '1': // ParseComplete
    callbacks_->incStatements();
    callbacks_->incStatementsOther();
    break;

  case 'T': // RowDescription
    decodeBackendRowDescription();
    break;

  case 'R': // AuthenticationOk
    callbacks_->incSessions();
    break;

  case 'E': // ErrorResponse
    decodeBackendErrorResponse();
    break;

  case 'N': // NoticeResponse
    decodeBackendNoticeResponse();
    break;

  default:
    callbacks_->incUnrecognized();
    break;
  }

  ENVOY_LOG(debug, "{} command = {}", command_type, command_);
  ENVOY_LOG(debug, "{} length = {}",  command_type, getMessageLength());
  ENVOY_LOG(debug, "{} message = {}", command_type, getMessage());
 /* 
  if (isBackend()) {
    decodeBackend();
  } else {
    decodeFrontend();
  }
*/
  ENVOY_LOG(trace, "postgresql_proxy: {} bytes remaining in buffer", data.length());
}


void DecoderImpl::onFrontendData(Buffer::Instance& data) {
  parseMessage(data);

  ENVOY_LOG(debug, "(Frontend) command = {}", command_);
  ENVOY_LOG(debug, "(Frontend) length = {}", getMessageLength());
  ENVOY_LOG(debug, "(Frontend) message = {}", getMessage());

}

void DecoderImpl::decodeBackend() {
  ENVOY_LOG(debug, "(Backend) command = {}", command_);
  ENVOY_LOG(debug, "(Backend) length = {}", getMessageLength());
  ENVOY_LOG(debug, "(Backend) message = {}", getMessage());

  // Decode the backend commands
  switch (command_)
  {
  case 'C': // CommandComplete
  case '2': // BindComplete
  case 'S': // ParameterStatus
    decodeBackendStatements();
    break;

  case '1': // ParseComplete
    callbacks_->incStatements();
    callbacks_->incStatementsOther();
    break;

  case 'T': // RowDescription
    decodeBackendRowDescription();
    break;

  case 'R': // AuthenticationOk
    callbacks_->incSessions();
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
  callbacks_->incStatements();
  if (getMessage().find("BEGIN") != std::string::npos) {
    callbacks_->incStatementsOther();
    session_.setInTransaction(true);
  } else if (getMessage().find("START TRANSACTION") != std::string::npos) {
    callbacks_->incStatementsOther();
    session_.setInTransaction(true);
  } else if (getMessage().find("ROLLBACK") != std::string::npos) {
    callbacks_->incStatementsOther();
    session_.setInTransaction(false);
    callbacks_->incTransactionsRollback();
  } else if (getMessage().find("COMMIT") != std::string::npos) {
    callbacks_->incStatementsOther();
    session_.setInTransaction(false);
    callbacks_->incTransactionsCommit();
  } else if (getMessage().find("INSERT") != std::string::npos) {
    callbacks_->incStatementsInsert();
    callbacks_->incTransactionsCommit();
  } else if (getMessage().find("UPDATE") != std::string::npos) {
    callbacks_->incStatementsUpdate();
    callbacks_->incTransactionsCommit();
  } else if (getMessage().find("DELETE") != std::string::npos) {
    callbacks_->incStatementsDelete();
    callbacks_->incTransactionsCommit();
  } else {
    callbacks_->incStatementsOther();
    callbacks_->incTransactionsCommit();
  }
}

void DecoderImpl::decodeBackendErrorResponse() {
  if (getMessage().find("VERROR") != std::string::npos) {
    callbacks_->incErrors();
  }
}

void DecoderImpl::decodeBackendNoticeResponse() {
  if (getMessage().find("VWARNING") != std::string::npos) {
    callbacks_->incWarnings();
  }
}

void DecoderImpl::decodeBackendRowDescription() {
  callbacks_->incStatements();
  callbacks_->incStatementsSelect();
  callbacks_->incTransactionsCommit();
}

void DecoderImpl::decodeFrontend() {
  ENVOY_LOG(debug, "(Frontend) command = {}", command_);
  ENVOY_LOG(debug, "(Frontend) length = {}", getMessageLength());
  ENVOY_LOG(debug, "(Frontend) message = {}", getMessage());

  switch (command_)
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
