#include "extensions/filters/network/postgresql_proxy/postgresql_decoder.h"

#include <vector>

#include "extensions/filters/network/postgresql_proxy/postgresql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

void DecoderImpl::initialize() {
  // Special handler for first message of the transaction
  first_ = Message{"Startup", {}};

  // Frontend messages
  std::get<0>(FEmessages_) = "Frontend";

  // Setup handlers for known messages
  absl::flat_hash_map<char, Message>& FE_known_msgs = std::get<1>(FEmessages_);

  // Handler for know messages
  FE_known_msgs['B'] = Message{"Bind", {&DecoderImpl::incFrontend}};
  FE_known_msgs['F'] = Message{"Close", {&DecoderImpl::incFrontend}};
  FE_known_msgs['d'] = Message{"CopyData", {}};
  FE_known_msgs['c'] = Message{"CopyDone", {}};
  FE_known_msgs['f'] = Message{"CopyFail", {&DecoderImpl::incFrontend}};
  FE_known_msgs['D'] = Message{"Describe", {&DecoderImpl::incFrontend}};
  FE_known_msgs['E'] = Message{"Execute", {&DecoderImpl::incFrontend}};
  FE_known_msgs['H'] = Message{"Flush", {&DecoderImpl::incFrontend}};
  FE_known_msgs['F'] = Message{"FunctionCall", {&DecoderImpl::incFrontend}};
  FE_known_msgs['p'] = Message{"GSSResponse", {&DecoderImpl::incFrontend}};
  FE_known_msgs['P'] = Message{"Parse", {&DecoderImpl::incFrontend}};
  FE_known_msgs['p'] = Message{"PasswordMessage", {&DecoderImpl::incFrontend}};
  FE_known_msgs['Q'] = Message{"Query", {&DecoderImpl::incFrontend}};
  FE_known_msgs['S'] = Message{"Sync", {&DecoderImpl::incFrontend}};
  FE_known_msgs['X'] = Message{"Terminate", {&DecoderImpl::decodeFrontendTerminate}};

  // Handler for unknown messages
  std::get<2>(FEmessages_) = Message{"Other", {&DecoderImpl::incUnrecognized}};

  // Backend messages
  std::get<0>(BEmessages_) = "Backend";

  // Setup handlers for known messages
  absl::flat_hash_map<char, Message>& BE_known_msgs = std::get<1>(BEmessages_);

  // Handler for know messages
  BE_known_msgs['R'] = Message{"Authentication", {&DecoderImpl::decodeAuthentication}};
  BE_known_msgs['K'] = Message{"BackendKeyData", {}};
  BE_known_msgs['2'] = Message{"BindComplete", {}};
  BE_known_msgs['3'] = Message{"CloseComplete", {}};
  BE_known_msgs['C'] = Message{"CommandComplete", {&DecoderImpl::decodeBackendStatements}};
  BE_known_msgs['d'] = Message{"CopyData", {}};
  BE_known_msgs['c'] = Message{"CopyDone", {}};
  BE_known_msgs['G'] = Message{"CopyInResponse", {}};
  BE_known_msgs['H'] = Message{"CopyOutResponse", {}};
  BE_known_msgs['D'] = Message{"DataRow", {}};
  BE_known_msgs['I'] = Message{"EmptyQueryResponse", {}};
  BE_known_msgs['E'] = Message{"ErrorResponse", {&DecoderImpl::decodeBackendErrorResponse}};
  BE_known_msgs['V'] = Message{"FunctionCallResponse", {}};
  BE_known_msgs['v'] = Message{"NegotiateProtocolVersion", {}};
  BE_known_msgs['n'] = Message{"NoData", {}};
  BE_known_msgs['N'] = Message{"NoticeResponse", {&DecoderImpl::decodeBackendNoticeResponse}};
  BE_known_msgs['A'] = Message{"NotificationResponse", {}};
  BE_known_msgs['t'] = Message{"ParameterDescription", {}};
  BE_known_msgs['S'] = Message{"ParameterStatus", {}};
  BE_known_msgs['1'] = Message{"ParseComplete", {}};
  BE_known_msgs['s'] = Message{"PortalSuspend", {}};
  BE_known_msgs['Z'] = Message{"ReadyForQuery", {}};
  BE_known_msgs['T'] = Message{"RowDescription", {}};

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

  if (!startup_) {
    char com;
    data.copyOut(0, 1, &com);
    ENVOY_LOG(trace, "postgresql_proxy: command is {}", com);
  }

  // The 1 byte message type and message length should be in the buffer
  // Check if the entire message has been read.
  std::string cmd;
  std::string message;
  uint32_t length;
  data.copyOut(startup_ ? 0 : 1, 4, &length);
  length = ntohl(length);
  if (data.length() < (length + (startup_ ? 0 : 1))) {
    ENVOY_LOG(trace, "postgresql_proxy: cannot parse message. Need {} bytes in buffer",
              length + (startup_ ? 0 : 1));
    // not enough data in the buffer
    return false;
  }

  setMessageLength(length);

  if (!startup_) {
    BufferHelper::readStringBySize(data, 1, cmd);
    command_ = cmd[0];
  }

  data.drain(4); // this is length which we already know.

  BufferHelper::readStringBySize(data, length - 4, message);
  setMessage(message);

  ENVOY_LOG(trace, "postgresql_proxy: msg parsed");
  return true;
}

bool DecoderImpl::onData(Buffer::Instance& data, bool frontend) {
  ENVOY_LOG(trace, "postgresql_proxy: decoding {} bytes", data.length());

  if (!parseMessage(data)) {
    return false;
  }

  std::tuple<std::string, absl::flat_hash_map<char, Message>, Message>& msg_processor =
      std::ref(frontend ? FEmessages_ : BEmessages_);

  std::reference_wrapper<Message> msg = std::get<2>(msg_processor);
  ;
  if (startup_) {
    msg = std::ref(first_);
    startup_ = false;
  } else {
    auto it = std::get<1>(msg_processor).find(command_);
    if (it != std::get<1>(msg_processor).end()) {
      msg = std::ref((*it).second);
    }
  }

  std::vector<MsgAction>& actions = std::get<1>(msg.get());
  for (const auto action : actions) {
    action(this);
  }

  ENVOY_LOG(debug, "{} command = {} ({})", std::get<0>(msg_processor), command_, std::get<0>(msg.get()));
  ENVOY_LOG(debug, "{} length = {}", std::get<0>(msg_processor), getMessageLength());
  ENVOY_LOG(debug, "{} message = {}", std::get<0>(msg_processor), getMessage());

  ENVOY_LOG(trace, "postgresql_proxy: {} bytes remaining in buffer", data.length());

  return true;
}

void DecoderImpl::decodeBackendStatements() {
  callbacks_->incStatements();
  if (getMessage().find("BEGIN") != std::string::npos) {
    callbacks_->incStatementsOther();
    session_.setInTransaction(true);
  } else if (getMessage().find("START") != std::string::npos) {
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
  } else if (getMessage().find("SELECT") != std::string::npos) {
    callbacks_->incStatementsSelect();
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

void DecoderImpl::decodeFrontendTerminate() {
  DecoderImpl::incFrontend();
  if (session_.inTransaction()) {
    session_.setInTransaction(false);
    callbacks_->incTransactionsRollback();
  }
}

void DecoderImpl::incFrontend() { callbacks_->incFrontend(); }
void DecoderImpl::incUnrecognized() { callbacks_->incUnrecognized(); }
void DecoderImpl::incStatements() { callbacks_->incStatements(); }
void DecoderImpl::incStatementsOther() { callbacks_->incStatementsOther(); }
void DecoderImpl::incSessions() { callbacks_->incSessions(); }

void DecoderImpl::decodeAuthentication() {
  // check if auth message indicates successful authentication
  // Length must be 8 and payload must be 0
  if ((8 == message_len_) && (0 == *(reinterpret_cast<const uint32_t*>(message_.data())))) {
    callbacks_->incSessions();
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

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
