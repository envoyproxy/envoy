#include "extensions/filters/network/postgres_proxy/postgres_decoder.h"

#include <vector>

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

void DecoderImpl::initialize() {
  // Special handler for first message of the transaction.
  first_ = MsgProcessor{"Startup", {&DecoderImpl::onStartup}};

  // Frontend messages.
  FE_messages_.direction_ = "Frontend";

  // Setup handlers for known messages.
  absl::flat_hash_map<char, MsgProcessor>& FE_known_msgs = FE_messages_.messages_;

  // Handler for known Frontend messages.
  FE_known_msgs['B'] = MsgProcessor{"Bind", {}};
  FE_known_msgs['C'] = MsgProcessor{"Close", {}};
  FE_known_msgs['d'] = MsgProcessor{"CopyData", {}};
  FE_known_msgs['c'] = MsgProcessor{"CopyDone", {}};
  FE_known_msgs['f'] = MsgProcessor{"CopyFail", {}};
  FE_known_msgs['D'] = MsgProcessor{"Describe", {}};
  FE_known_msgs['E'] = MsgProcessor{"Execute", {}};
  FE_known_msgs['H'] = MsgProcessor{"Flush", {}};
  FE_known_msgs['F'] = MsgProcessor{"FunctionCall", {}};
  FE_known_msgs['p'] =
      MsgProcessor{"PasswordMessage/GSSResponse/SASLInitialResponse/SASLResponse", {}};
  FE_known_msgs['P'] = MsgProcessor{"Parse", {&DecoderImpl::onParse}};
  FE_known_msgs['Q'] = MsgProcessor{"Query", {&DecoderImpl::onQuery}};
  FE_known_msgs['S'] = MsgProcessor{"Sync", {}};
  FE_known_msgs['X'] = MsgProcessor{"Terminate", {&DecoderImpl::decodeFrontendTerminate}};

  // Handler for unknown Frontend messages.
  FE_messages_.unknown_ = MsgProcessor{"Other", {&DecoderImpl::incMessagesUnknown}};

  // Backend messages.
  BE_messages_.direction_ = "Backend";

  // Setup handlers for known messages.
  absl::flat_hash_map<char, MsgProcessor>& BE_known_msgs = BE_messages_.messages_;

  // Handler for known Backend messages.
  BE_known_msgs['R'] = MsgProcessor{"Authentication", {&DecoderImpl::decodeAuthentication}};
  BE_known_msgs['K'] = MsgProcessor{"BackendKeyData", {}};
  BE_known_msgs['2'] = MsgProcessor{"BindComplete", {}};
  BE_known_msgs['3'] = MsgProcessor{"CloseComplete", {}};
  BE_known_msgs['C'] = MsgProcessor{"CommandComplete", {&DecoderImpl::decodeBackendStatements}};
  BE_known_msgs['d'] = MsgProcessor{"CopyData", {}};
  BE_known_msgs['c'] = MsgProcessor{"CopyDone", {}};
  BE_known_msgs['G'] = MsgProcessor{"CopyInResponse", {}};
  BE_known_msgs['H'] = MsgProcessor{"CopyOutResponse", {}};
  BE_known_msgs['D'] = MsgProcessor{"DataRow", {}};
  BE_known_msgs['I'] = MsgProcessor{"EmptyQueryResponse", {}};
  BE_known_msgs['E'] = MsgProcessor{"ErrorResponse", {&DecoderImpl::decodeBackendErrorResponse}};
  BE_known_msgs['V'] = MsgProcessor{"FunctionCallResponse", {}};
  BE_known_msgs['v'] = MsgProcessor{"NegotiateProtocolVersion", {}};
  BE_known_msgs['n'] = MsgProcessor{"NoData", {}};
  BE_known_msgs['N'] = MsgProcessor{"NoticeResponse", {&DecoderImpl::decodeBackendNoticeResponse}};
  BE_known_msgs['A'] = MsgProcessor{"NotificationResponse", {}};
  BE_known_msgs['t'] = MsgProcessor{"ParameterDescription", {}};
  BE_known_msgs['S'] = MsgProcessor{"ParameterStatus", {}};
  BE_known_msgs['1'] = MsgProcessor{"ParseComplete", {}};
  BE_known_msgs['s'] = MsgProcessor{"PortalSuspend", {}};
  BE_known_msgs['Z'] = MsgProcessor{"ReadyForQuery", {}};
  BE_known_msgs['T'] = MsgProcessor{"RowDescription", {}};

  // Handler for unknown Backend messages.
  BE_messages_.unknown_ = MsgProcessor{"Other", {&DecoderImpl::incMessagesUnknown}};

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
  BE_errors_.keywords_["ERROR"] = [this](DecoderImpl*) -> void {
    callbacks_->incErrors(DecoderCallbacks::ErrorType::Error);
  };
  BE_errors_.keywords_["FATAL"] = [this](DecoderImpl*) -> void {
    callbacks_->incErrors(DecoderCallbacks::ErrorType::Fatal);
  };
  BE_errors_.keywords_["PANIC"] = [this](DecoderImpl*) -> void {
    callbacks_->incErrors(DecoderCallbacks::ErrorType::Panic);
  };
  // Setup handler which is called when decoder cannot decode the message and treats it as Unknown
  // Error message.
  BE_errors_.unknown_ = [this](DecoderImpl*) -> void {
    callbacks_->incErrors(DecoderCallbacks::ErrorType::Unknown);
  };

  // Setup hash map for handling backend NoticeResponse messages.
  BE_notices_.keywords_["WARNING"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Warning);
  };
  BE_notices_.keywords_["NOTICE"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Notice);
  };
  BE_notices_.keywords_["DEBUG"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Debug);
  };
  BE_notices_.keywords_["INFO"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Info);
  };
  BE_notices_.keywords_["LOG"] = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Log);
  };
  // Setup handler which is called when decoder cannot decode the message and treats it as Unknown
  // Notice message.
  BE_notices_.unknown_ = [this](DecoderImpl*) -> void {
    callbacks_->incNotices(DecoderCallbacks::NoticeType::Unknown);
  };
}

bool DecoderImpl::parseMessage(Buffer::Instance& data) {
  ENVOY_LOG(trace, "postgres_proxy: parsing message, len {}", data.length());

  // The minimum size of the message sufficient for parsing is 5 bytes.
  if (data.length() < 5) {
    // not enough data in the buffer.
    return false;
  }

  if (!startup_) {
    data.copyOut(0, 1, &command_);
    ENVOY_LOG(trace, "postgres_proxy: command is {}", command_);
  }

  // The 1 byte message type and message length should be in the buffer
  // Check if the entire message has been read.
  std::string message;

  uint32_t length = data.peekBEInt<uint32_t>(startup_ ? 0 : 1);
  if (data.length() < (length + (startup_ ? 0 : 1))) {
    ENVOY_LOG(trace, "postgres_proxy: cannot parse message. Need {} bytes in buffer",
              length + (startup_ ? 0 : 1));
    // Not enough data in the buffer.
    return false;
  }

  if (startup_) {
    uint32_t code = data.peekBEInt<uint32_t>(4);
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
      // 4 bytes of length and 4 bytes of version code.
    }
  }

  setMessageLength(length);

  data.drain(startup_ ? 4 : 5); // Length plus optional 1st byte.

  auto bytesToRead = length - 4;
  message.assign(std::string(static_cast<char*>(data.linearize(bytesToRead)), bytesToRead));
  data.drain(bytesToRead);
  setMessage(message);

  ENVOY_LOG(trace, "postgres_proxy: msg parsed");
  return true;
}

bool DecoderImpl::onData(Buffer::Instance& data, bool frontend) {
  // If encrypted, just drain the traffic.
  if (encrypted_) {
    ENVOY_LOG(trace, "postgres_proxy: ignoring {} bytes of encrypted data", data.length());
    data.drain(data.length());
    return true;
  }

  ENVOY_LOG(trace, "postgres_proxy: decoding {} bytes", data.length());

  if (!parseMessage(data)) {
    return false;
  }

  MsgGroup& msg_processor = std::ref(frontend ? FE_messages_ : BE_messages_);
  frontend ? callbacks_->incMessagesFrontend() : callbacks_->incMessagesBackend();

  // Set processing to the handler of unknown messages.
  // If message is found, the processing will be updated.
  std::reference_wrapper<MsgProcessor> msg = msg_processor.unknown_;

  if (startup_) {
    msg = std::ref(first_);
    startup_ = false;
  } else {
    auto it = msg_processor.messages_.find(command_);
    if (it != msg_processor.messages_.end()) {
      msg = std::ref((*it).second);
    }
  }

  std::vector<MsgAction>& actions = std::get<1>(msg.get());
  for (const auto& action : actions) {
    action(this);
  }

  ENVOY_LOG(debug, "({}) command = {} ({})", msg_processor.direction_, command_,
            std::get<0>(msg.get()));
  ENVOY_LOG(debug, "({}) length = {}", msg_processor.direction_, getMessageLength());
  ENVOY_LOG(debug, "({}) message = {}", msg_processor.direction_, getMessage());

  ENVOY_LOG(trace, "postgres_proxy: {} bytes remaining in buffer", data.length());

  return true;
}

// Method is called when C (CommandComplete) message has been
// decoded. It extracts the keyword from message's payload
// and updates stats associated with that keyword.
void DecoderImpl::decodeBackendStatements() {
  // The message_ contains the statement. Find space character
  // and the statement is the first word. If space cannot be found
  // take the whole message.
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
  // Check if auth message indicates successful authentication.
  // Length must be 8 and payload must be 0.
  if ((8 == message_len_) && (0 == message_.data()[0]) && (0 == message_.data()[1]) &&
      (0 == message_.data()[2]) && (0 == message_.data()[3])) {
    incSessionsUnencrypted();
  }
}

// Method is used to parse Error and Notice messages. Their syntax is the same, but they
// use different keywords inside the message and statistics fields are different.
void DecoderImpl::decodeErrorNotice(MsgParserDict& types) {
  // Error/Notice message should start with character "S".
  if (message_[0] != 'S') {
    types.unknown_(this);
    return;
  }

  for (const auto& it : types.keywords_) {
    // Try to find a keyword with S prefix or V prefix.
    // Postgres versions prior to 9.6 use only S prefix while
    // versions higher than 9.6 use S and V prefixes.
    if ((message_.find("S" + it.first) != std::string::npos) ||
        (message_.find("V" + it.first) != std::string::npos)) {
      it.second(this);
      return;
    }
  }

  // Keyword was not found in the message. Count is as Unknown.
  types.unknown_(this);
}

// Method parses E (Error) message and looks for string
// indicating that error happened.
void DecoderImpl::decodeBackendErrorResponse() { decodeErrorNotice(BE_errors_); }

// Method parses N (Notice) message and looks for string
// indicating its meaning. It can be warning, notice, info, debug or log.
void DecoderImpl::decodeBackendNoticeResponse() { decodeErrorNotice(BE_notices_); }

// Method parses Parse message of the following format:
// String: The name of the destination prepared statement (an empty string selects the unnamed
// prepared statement).
//
// String: The query string to be parsed.
//
// Int16: The number of parameter data
// types specified (can be zero). Note that this is not an indication of the number of parameters
// that might appear in the query string, only the number that the frontend wants to pre-specify
// types for. Then, for each parameter, there is the following:
//
// Int32: Specifies the object ID of
// the parameter data type. Placing a zero here is equivalent to leaving the type unspecified.
void DecoderImpl::onParse() {
  // The first two strings are separated by \0.
  // The first string is optional. If no \0 is found it means
  // that the message contains query string only.
  std::vector<std::string> query_parts = absl::StrSplit(message_, absl::ByChar('\0'));
  callbacks_->processQuery(query_parts[1]);
}

void DecoderImpl::onQuery() { callbacks_->processQuery(message_); }

// Method is invoked on clear-text Startup message.
// The message format is continuous string of the following format:
// user<username>database<database-name>application_name<application>encoding<encoding-type>
void DecoderImpl::onStartup() {
  // First 4 bytes of startup message contains version code.
  // It is skipped. After that message contains attributes.
  attributes_ = absl::StrSplit(message_.substr(4), absl::ByChar('\0'), absl::SkipEmpty());

  // If "database" attribute is not found, default it to "user" attribute.
  if ((attributes_.find("database") == attributes_.end()) &&
      (attributes_.find("user") != attributes_.end())) {
    attributes_["database"] = attributes_["user"];
  }
}

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
