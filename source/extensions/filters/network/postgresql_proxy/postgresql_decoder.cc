#include "extensions/filters/network/postgresql_proxy/postgresql_decoder.h"
#include "extensions/filters/network/postgresql_proxy/postgresql_utils.h"

#include <vector>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

void DecoderImpl::initialize() {
  // Special handler for first message of the transaction
  first_ = Message{"Initial", {}};

  // Frontend messages
  std::get<0>(FEmessages_) = "Frontend";
  // Setup handlers for known messages
  absl::flat_hash_map<char, Message>& FE_known_msgs = std::get<1>(FEmessages_);
  FE_known_msgs['Q'] = Message{"Q(Query)", {&DecoderImpl::incFrontend}};
  FE_known_msgs['P'] = Message{"P(Parse)", {&DecoderImpl::incFrontend}};
  FE_known_msgs['B'] = Message{"B(Bind)", {&DecoderImpl::incFrontend}};
  // Handler for unknown messages
  std::get<2>(FEmessages_) = Message{"Other", {&DecoderImpl::incUnrecognized}};
  
  // Backend messages
  std::get<0>(BEmessages_) = "Backend";
  // Setup handlers for known messages
  //absl::flat_hash_map<char, Message>& BE_known_msgs = std::get<1>(BEmessages_);
  // Handler for unknown messages
  std::get<2>(BEmessages_) = Message{"Other", {&DecoderImpl::incUnrecognized}};
}

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


  setMessageLength(length);

  if(!initial_) {
  BufferHelper::readStringBySize(data, 1, cmd);
  command_ = cmd[0];
  }
  //setCommand(cmd);

  data.drain(4); // this is length which we already know.

  BufferHelper::readStringBySize(data, length - 4, message);
  setMessage(message);

  ENVOY_LOG(trace, "postgresql_proxy: msg parsed");
  return true;
}

bool DecoderImpl::onData(Buffer::Instance& data, bool frontend) {
  ENVOY_LOG(trace, "postgresql_proxy: decoding {} bytes", data.length());

  if(!parseMessage(data)) {
    return false;
  }
  

  std::tuple<std::string, absl::flat_hash_map<char, Message>, Message>& msg_processor = std::ref(frontend ? FEmessages_ : BEmessages_); 

  Message& msg = std::ref(std::get<2>(msg_processor));;
  if(initial_) {
	  msg = std::ref(first_);
	  initial_ = false;
  } else {
  auto it = std::get<1>(msg_processor).find(command_);
  if (it != std::get<1>(msg_processor).end()) {
     msg = std::ref((*it).second);
  }
  }

  std::vector<MsgAction>& actions = std::get<1>(msg);
  for (const auto action : actions) {
    action(this);
  } 

#if 0
  std::reference_wrapper<const std::unique_ptr<MessageImpl>> msg = std::cref(unrecognized_); 
  auto it = messages_.find(command_);
  if (it != messages_.end()) {
     msg = std::cref((*it).second);
  }

  for (const auto action : msg.get()->getActions()) {
    action(this);
  } 
#endif
#if 0 

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
#endif

  ENVOY_LOG(debug, "{} command = {}", std::get<0>(msg_processor), std::get<0>(msg));
  ENVOY_LOG(debug, "{} length = {}",  std::get<0>(msg_processor), getMessageLength());
  ENVOY_LOG(debug, "{} message = {}", std::get<0>(msg_processor), getMessage());
 /* 
  if (isBackend()) {
    decodeBackend();
  } else {
    decodeFrontend();
  }
*/
  ENVOY_LOG(trace, "postgresql_proxy: {} bytes remaining in buffer", data.length());

  return true;
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
void DecoderImpl::incFrontend() {
    callbacks_->incFrontend();
}

void DecoderImpl::incUnrecognized() {
    callbacks_->incUnrecognized();
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
#if 0
void DecoderImpl::onData(Buffer::Instance& data) {
  decode(data);
}
#endif

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
