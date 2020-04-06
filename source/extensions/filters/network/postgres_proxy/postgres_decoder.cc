#include "extensions/filters/network/postgres_proxy/postgres_decoder.h"

#include <vector>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

void DecoderImpl::initialize() {
  // Special handler for first message of the transaction
  first_ = Message{"Startup", {}};

  // Frontend messages
  std::get<0>(FE_messages_) = "Frontend";

  // Setup handlers for known messages
  absl::flat_hash_map<char, Message>& FE_known_msgs = std::get<1>(FE_messages_);

  // Handler for know messages
  FE_known_msgs['B'] = Message{"Bind", {}};
  FE_known_msgs['C'] = Message{"Close", {}};
  FE_known_msgs['d'] = Message{"CopyData", {}};
  FE_known_msgs['c'] = Message{"CopyDone", {}};
  FE_known_msgs['f'] = Message{"CopyFail", {}};
  FE_known_msgs['D'] = Message{"Describe", {}};
  FE_known_msgs['E'] = Message{"Execute", {}};
  FE_known_msgs['H'] = Message{"Flush", {}};
  FE_known_msgs['F'] = Message{"FunctionCall", {}};
  FE_known_msgs['p'] = Message{"PasswordMessage/GSSResponse/SASLInitialResponse/SASLResponse", {}};
  FE_known_msgs['P'] = Message{"Parse", {}};
  FE_known_msgs['Q'] = Message{"Query", {}};
  FE_known_msgs['S'] = Message{"Sync", {}};
  FE_known_msgs['X'] = Message{"Terminate", {&DecoderImpl::decodeFrontendTerminate}};

  // Handler for unknown messages
  std::get<2>(FE_messages_) = Message{"Other", {&DecoderImpl::incMessagesUnknown}};

  // Backend messages
  std::get<0>(BE_messages_) = "Backend";

  // Setup handlers for known messages
  absl::flat_hash_map<char, Message>& BE_known_msgs = std::get<1>(BE_messages_);

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
  std::get<2>(BE_messages_) = Message{"Other", {&DecoderImpl::incMessagesUnknown}};

  // Setup hash map for handling backend statements.
  BE_statements_["BEGIN"] = [this](DecoderImpl*) -> void {
    callbacks_->incStatements(DecoderCallbacks::StatementType::Other);
    callbacks_->incTransactions();
    session_.setInTransaction(true);
  };
  BE_statements_["ROLLBACK"] = [this](DecoderImpl*) -> void {
    callbacks_->incStatements(DecoderCallbacks::StatementType::Noop);
    callbacks_->incTransactionsRollback();
    session_.setInTransaction(false);
  };
  BE_statements_["START"] = [this](DecoderImpl*) -> void {
    callbacks_->incStatements(DecoderCallbacks::StatementType::Other);
    callbacks_->incTransactions();
    session_.setInTransaction(true);
  };
  BE_statements_["COMMIT"] = [this](DecoderImpl*) -> void {
    callbacks_->incStatements(DecoderCallbacks::StatementType::Noop);
    session_.setInTransaction(false);
    callbacks_->incTransactionsCommit();
  };
  BE_statements_["SELECT"] = [this](DecoderImpl*) -> void {
    callbacks_->incStatements(DecoderCallbacks::StatementType::Select);
    callbacks_->incTransactions();
    callbacks_->incTransactionsCommit();
  };
  BE_statements_["INSERT"] = [this](DecoderImpl*) -> void {
    callbacks_->incStatements(DecoderCallbacks::StatementType::Insert);
    callbacks_->incTransactions();
    callbacks_->incTransactionsCommit();
  };
  BE_statements_["UPDATE"] = [this](DecoderImpl*) -> void {
    callbacks_->incStatements(DecoderCallbacks::StatementType::Update);
    callbacks_->incTransactions();
    callbacks_->incTransactionsCommit();
  };
  BE_statements_["DELETE"] = [this](DecoderImpl*) -> void {
    callbacks_->incStatements(DecoderCallbacks::StatementType::Delete);
    callbacks_->incTransactions();
    callbacks_->incTransactionsCommit();
  };

  // Setup hash map for handling backend ErrorResponse messages.
  std::get<0>(BE_errors_)["ERROR"] = [this](DecoderImpl*) -> void {
    callbacks_->incErrors(DecoderCallbacks::ErrorType::Error);
  };
  std::get<0>(BE_errors_)["FATAL"] = [this](DecoderImpl*) -> void {
    callbacks_->incErrors(DecoderCallbacks::ErrorType::Fatal);
  };
  std::get<0>(BE_errors_)["PANIC"] = [this](DecoderImpl*) -> void {
    callbacks_->incErrors(DecoderCallbacks::ErrorType::Panic);
  };
  // Setup handler which is called when decoder cannot decode the message and treats it as Unknown
  // Error message.
  std::get<1>(BE_errors_) = [this](DecoderImpl*) -> void {
    callbacks_->incErrors(DecoderCallbacks::ErrorType::Unknown);
  };

  // Setup hash map for handling backend NoticeResponse messages.
  std::get<0>(BE_notices_)["WARNING"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Warning);
  };
  std::get<0>(BE_notices_)["NOTICE"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Notice);
  };
  std::get<0>(BE_notices_)["DEBUG"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Debug);
  };
  std::get<0>(BE_notices_)["INFO"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Info);
  };
  std::get<0>(BE_notices_)["LOG"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Log);
  };
  // Setup handler which is called when decoder cannot decode the message and treats it as Unknown
  // Notice message.
  std::get<1>(BE_notices_) = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Unknown);
  };
}

bool DecoderImpl::parseMessage(Buffer::Instance& data) {
  ENVOY_LOG(trace, "postgres_proxy: parsing message, len {}", data.length());

  // The minimum size of the message sufficient for parsing is 5 bytes.
  if (data.length() < 5) {
    // not enough data in the buffer
    return false;
  }

  if (!startup_) {
    data.copyOut(0, 1, &command_);
    ENVOY_LOG(trace, "postgres_proxy: command is {}", command_);
  }

  // The 1 byte message type and message length should be in the buffer
  // Check if the entire message has been read.
  std::string message;
  uint32_t length;
  data.copyOut(startup_ ? 0 : 1, 4, &length);
  length = ntohl(length);
  if (data.length() < (length + (startup_ ? 0 : 1))) {
    ENVOY_LOG(trace, "postgres_proxy: cannot parse message. Need {} bytes in buffer",
              length + (startup_ ? 0 : 1));
    // not enough data in the buffer
    return false;
  }

  if (startup_) {
    uint32_t code;
    data.copyOut(4, 4, &code);
    code = ntohl(code);
    // Startup message with 1234 in the most significant 16 bits
    // indicate request to encrypt.
    if (code >= 0x04d20000) {
      ENVOY_LOG(trace, "postgres_proxy: detected encrypted traffic.");
      encrypted_ = true;
      startup_ = false;
      incSessionsEncrypted();
      data.drain(data.length());
      return false;
    } else {
      ENVOY_LOG(debug, "Detected version {}.{} of Postgres", code >> 16, code & 0x0000FFFF);
    }
  }

  setMessageLength(length);

  data.drain(startup_ ? 4 : 5); // length plus optional 1st byte

  auto bytesToRead = length - 4;
  message.assign(std::string(static_cast<char*>(data.linearize(bytesToRead)), bytesToRead));
  data.drain(bytesToRead);
  setMessage(message);

  ENVOY_LOG(trace, "postgres_proxy: msg parsed");
  return true;
}

bool DecoderImpl::onData(Buffer::Instance& data, bool frontend) {
  // If encrypted, just drain the traffic
  if (encrypted_) {
    ENVOY_LOG(trace, "postgres_proxy: ignoring {} bytes of encrypted data", data.length());
    data.drain(data.length());
    return true;
  }

  ENVOY_LOG(trace, "postgres_proxy: decoding {} bytes", data.length());

  if (!parseMessage(data)) {
    return false;
  }

  std::tuple<std::string, absl::flat_hash_map<char, Message>, Message>& msg_processor =
      std::ref(frontend ? FE_messages_ : BE_messages_);
  frontend ? callbacks_->incMessagesFrontend() : callbacks_->incMessagesBackend();

  std::reference_wrapper<Message> msg = std::get<2>(msg_processor);

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
  for (const auto& action : actions) {
    action(this);
  }

  ENVOY_LOG(debug, "({}) command = {} ({})", std::get<0>(msg_processor), command_,
            std::get<0>(msg.get()));
  ENVOY_LOG(debug, "({}) length = {}", std::get<0>(msg_processor), getMessageLength());
  ENVOY_LOG(debug, "({}) message = {}", std::get<0>(msg_processor), getMessage());

  ENVOY_LOG(trace, "postgres_proxy: {} bytes remaining in buffer", data.length());

  return true;
}

// Method is called when C (CommandComplete) message has been
// decoded. It extracts the keyword from message's payload
// and updates stats associated with that keyword.
void DecoderImpl::decodeBackendStatements() {
  // The message_ contains the statement. Find space character
  // and the statement is the first word. If space cannot be found
  // take the whole message
  std::string statement = message_.substr(0, message_.find(' '));

  auto it = BE_statements_.find(statement);
  if (it != BE_statements_.end()) {
    (*it).second(this);
  } else {
    callbacks_->incStatements(DecoderCallbacks::StatementType::Other);
    callbacks_->incTransactions();
    callbacks_->incTransactionsCommit();
  }
}

// Method is called when X (Terminate) message
// is encountered by the decoder.
void DecoderImpl::decodeFrontendTerminate() {
  if (session_.inTransaction()) {
    session_.setInTransaction(false);
    callbacks_->incTransactionsRollback();
  }
}

// Method does deep inspection of Authentication message.
// It looks for 4 bytes of zeros, which means that login to
// database was successful.
void DecoderImpl::decodeAuthentication() {
  // check if auth message indicates successful authentication
  // Length must be 8 and payload must be 0
  if ((8 == message_len_) && (0 == message_.data()[0]) && (0 == message_.data()[1]) &&
      (0 == message_.data()[2]) && (0 == message_.data()[3])) {
    incSessionsUnencrypted();
  }
}

// Method is used to parse Error and Notice messages. Their syntax is the same, but they
// use different keywords inside the message and statistics fields are different.
void DecoderImpl::decodeErrorNotice(
    std::tuple<absl::flat_hash_map<std::string, MsgAction>, MsgAction>& types) {
  // Error/Notice message should start with character "S"
  if (message_[0] != 'S') {
    std::get<1>(types)(this);
    return;
  }

  for (const auto& it : std::get<0>(types)) {
    // Try to find a keyword with S prefix or V prefix.
    // Postgres versions prior to 9.6 use only S prefix while
    // versions higher than 9.6 use S and V prefixes.
    if ((message_.find("S" + it.first) != std::string::npos) ||
        (message_.find("V" + it.first) != std::string::npos)) {
      it.second(this);
      return;
    }
  }

  // keyword was not found in the message. Count is as Unknown.
  std::get<1>(types)(this);
}

// Method parses E (Error) message and looks for string
// indicating that error happened.
void DecoderImpl::decodeBackendErrorResponse() { decodeErrorNotice(BE_errors_); }

// Method parses N (Notice) message and looks for string
// indicating its meaning. It can be warning, notice, info, debug or log.
void DecoderImpl::decodeBackendNoticeResponse() { decodeErrorNotice(BE_notices_); }

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
