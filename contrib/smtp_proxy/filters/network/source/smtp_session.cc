#include "contrib/smtp_proxy/filters/network/source/smtp_session.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

SmtpSession::SmtpSession(DecoderCallbacks* callbacks, TimeSource& time_source,
                         Random::RandomGenerator& random_generator)
    : callbacks_(callbacks), time_source_(time_source), random_generator_(random_generator) {}
void SmtpSession::newCommand(const std::string& name, SmtpCommand::Type type) {
  current_command_ = std::make_shared<SmtpCommand>(name, type, time_source_);
  command_in_progress_ = true;
}

void SmtpSession::createNewTransaction() {
  if (smtp_transaction_ == nullptr) {
    smtp_transaction_ =
        new SmtpTransaction(session_id_, callbacks_, time_source_, random_generator_);
    transaction_in_progress_ = true;
  }
}

void SmtpSession::updateBytesMeterOnCommand(Buffer::Instance& data) {

  if (data.length() == 0)
    return;

  if (transaction_in_progress_) {
    getTransaction()->getStreamInfo().addBytesReceived(data.length());
    getTransaction()->getStreamInfo().getDownstreamBytesMeter()->addWireBytesReceived(
        data.length());
    getTransaction()->getStreamInfo().getUpstreamBytesMeter()->addHeaderBytesSent(data.length());

    if (isDataTransferInProgress()) {
      getTransaction()->addPayloadBytes(data.length());
    }
  }
}

void SmtpSession::updateBytesMeterOnResponse(Buffer::Instance& data) {
  if (data.length() == 0)
    return;

  if (transaction_in_progress_) {
    getTransaction()->getStreamInfo().addBytesSent(data.length());
    getTransaction()->getStreamInfo().getDownstreamBytesMeter()->addWireBytesSent(data.length());
    getTransaction()->getStreamInfo().getUpstreamBytesMeter()->addWireBytesReceived(data.length());
  }
}

SmtpUtils::Result SmtpSession::handleCommand(std::string& command, std::string& args) {

  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  if (command == "")
    return result;

  if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpEhloCommand) ||
      absl::EqualsIgnoreCase(command, SmtpUtils::smtpHeloCommand)) {
    result = handleEhlo(command);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpMailCommand)) {
    result = handleMail(args);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpRcptCommand)) {
    result = handleRcpt(args);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpDataCommand)) {
    result = handleData(args);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpAuthCommand)) {
    result = handleAuth();
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::startTlsCommand)) {
    result = handleStarttls();
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpRsetCommand)) {
    result = handleReset(args);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpQuitCommand)) {
    result = handleQuit(args);
  } else {
    result = handleOtherCmds(command);
  }
  return result;
}

SmtpUtils::Result SmtpSession::handleEhlo(std::string& command) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  newCommand(StringUtil::toUpper(command), SmtpCommand::Type::NonTransactionCommand);
  setState(SmtpSession::State::SessionInitRequest);

  return result;
}

SmtpUtils::Result SmtpSession::handleMail(std::string& arg) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (state_ != SmtpSession::State::SessionInProgress) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, "Please introduce yourself first"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }

  if (arg.length() < 6 || !absl::EqualsIgnoreCase(arg.substr(0, 5), "FROM:")) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(501, {5, 5, 2}, "Bad MAIL arg syntax of FROM:<address>"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }
  newCommand(SmtpUtils::smtpMailCommand, SmtpCommand::Type::TransactionCommand);
  createNewTransaction();
  std::string sender = SmtpUtils::extractAddress(arg);
  getTransaction()->setSender(sender);
  updateBytesMeterOnCommand(callbacks_->getReadBuffer());
  setTransactionState(SmtpTransaction::State::TransactionRequest);
  return result;
}

SmtpUtils::Result SmtpSession::handleRcpt(std::string& arg) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (getTransaction() == nullptr) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, "Missing MAIL FROM command"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }

  if (arg.length() <= 3 || !absl::EqualsIgnoreCase(arg.substr(0, 3), "TO:")) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(501, {5, 5, 2}, "Bad RCPT arg syntax of TO:<address>"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }

  newCommand(SmtpUtils::smtpRcptCommand, SmtpCommand::Type::TransactionCommand);
  std::string recipient = SmtpUtils::extractAddress(arg);
  getTransaction()->addRcpt(recipient);
  updateBytesMeterOnCommand(callbacks_->getReadBuffer());
  setTransactionState(SmtpTransaction::State::RcptCommand);
  return result;
}

SmtpUtils::Result SmtpSession::handleData(std::string& arg) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (arg.length() > 0) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(501, {5, 5, 4}, "No params allowed for DATA command"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }

  if (getTransaction() == nullptr || getTransaction()->getNoOfRecipients() == 0) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, "Missing RCPT TO command"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }

  newCommand(SmtpUtils::smtpDataCommand, SmtpCommand::Type::TransactionCommand);
  updateBytesMeterOnCommand(callbacks_->getReadBuffer());
  setDataTransferInProgress(true);
  setTransactionState(SmtpTransaction::State::MailDataTransferRequest);
  return result;
}

SmtpUtils::Result SmtpSession::handleAuth() {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (state_ != SmtpSession::State::SessionInProgress) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, "Please introduce yourself first"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }

  if (isAuthenticated()) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, "Already authenticated"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }

  newCommand(SmtpUtils::smtpAuthCommand, SmtpCommand::Type::NonTransactionCommand);
  setState(SmtpSession::State::SessionAuthRequest);
  return result;
}

SmtpUtils::Result SmtpSession::handleStarttls() {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (isSessionEncrypted()) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, "Already running in TLS"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }
  newCommand(SmtpUtils::startTlsCommand, SmtpCommand::Type::NonTransactionCommand);
  if (callbacks_->upstreamTlsRequired()) {
    // Send STARTTLS request to upstream.
    setState(SmtpSession::State::UpstreamTlsNegotiation);
  } else {
    // Perform downstream TLS negotiation.
    handleDownstreamTls();
    result = SmtpUtils::Result::Stopped;
  }
  return result;
}

SmtpUtils::Result SmtpSession::handleReset(std::string& arg) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (arg.length() > 0) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(501, {5, 5, 4}, "No params allowed for RSET command"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }

  newCommand(SmtpUtils::smtpRsetCommand, SmtpCommand::Type::NonTransactionCommand);
  if (getTransaction() != nullptr) {
    setTransactionState(SmtpTransaction::State::TransactionAbortRequest);
  }
  return result;
}

SmtpUtils::Result SmtpSession::handleQuit(std::string& arg) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (arg.length() > 0) {
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(501, {5, 5, 4}, "No params allowed for QUIT command"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }
  newCommand(SmtpUtils::smtpQuitCommand, SmtpCommand::Type::NonTransactionCommand);
  setState(SmtpSession::State::SessionTerminationRequest);
  if (getTransaction() != nullptr) {
    setTransactionState(SmtpTransaction::State::TransactionAbortRequest);
  }
  return result;
}

SmtpUtils::Result SmtpSession::handleOtherCmds(std::string& command) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  newCommand(StringUtil::toUpper(command), SmtpCommand::Type::NonTransactionCommand);
  return result;
}

SmtpUtils::Result SmtpSession::handleResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  // Special handling to parse any error response to connection request.
  if (getState() == SmtpSession::State::ConnectionRequest) {
    return handleConnResponse(response_code, response);
  }
  if (!isCommandInProgress()) {
    return result;
  }
  std::string command = getCurrentCommand()->getName();

  if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpEhloCommand) ||
      absl::EqualsIgnoreCase(command, SmtpUtils::smtpHeloCommand)) {
    result = handleEhloResponse(response_code, response);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpMailCommand)) {
    result = handleMailResponse(response_code, response);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpRcptCommand)) {
    result = handleRcptResponse(response_code, response);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpDataCommand)) {
    result = handleDataResponse(response_code, response);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpAuthCommand)) {
    result = handleAuthResponse(response_code, response);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::startTlsCommand)) {
    result = handleStarttlsResponse(response_code, response);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpRsetCommand)) {
    result = handleResetResponse(response_code, response);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::smtpQuitCommand)) {
    result = handleQuitResponse(response_code, response);
  } else if (absl::EqualsIgnoreCase(command, SmtpUtils::xReqIdCommand)) {
    result = handleXReqIdResponse(response_code, response);
  } else {
    result = handleOtherResponse(response_code, response);
  }

  return result;
}

SmtpUtils::Result SmtpSession::handleConnResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  auto stream_id_provider = callbacks_->getStreamInfo().getStreamIdProvider();
  if (stream_id_provider.has_value()) {
    session_id_ = stream_id_provider->toStringView().value_or("");
  }

  if (response_code == 220) {
    setState(SmtpSession::State::ConnectionSuccess);
    if (callbacks_->tracingEnabled() && !isXReqIdSent()) {
      response_on_hold_ = response + SmtpUtils::smtpCrlfSuffix;
      std::string x_req_id = SmtpUtils::xReqIdCommand + session_id_ + "\r\n";
      Buffer::OwnedImpl data(x_req_id);
      newCommand(SmtpUtils::xReqIdCommand, SmtpCommand::Type::NonTransactionCommand);
      callbacks_->sendUpstream(data);
      setState(SmtpSession::State::XReqIdTransfer);
      result = SmtpUtils::Result::Stopped;
      return result;
    }
  } else if (response_code == 554) {
    callbacks_->incSmtpConnectionEstablishmentErrors();
  }
  return result;
}

SmtpUtils::Result SmtpSession::handleEhloResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  if (response_code == 250) {
    setState(SmtpSession::State::SessionInProgress);
    if (transaction_in_progress_) {
      // Abort this transaction.
      abortTransaction();
    }
  } else {
    setState(SmtpSession::State::ConnectionSuccess);
  }

  storeResponse(response, response_code);
  return result;
}

SmtpUtils::Result SmtpSession::handleMailResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, response_code);
  updateBytesMeterOnResponse(callbacks_->getWriteBuffer());
  if (response_code == 250) {
    setTransactionState(SmtpTransaction::State::TransactionInProgress);
    if (callbacks_->tracingEnabled() && !getTransaction()->isXReqIdSent()) {
      response_on_hold_ = response + SmtpUtils::smtpCrlfSuffix;
      std::string x_req_id =
          SmtpUtils::xReqIdCommand + getTransaction()->getTransactionId() + "\r\n";
      Buffer::OwnedImpl data(x_req_id);
      newCommand(SmtpUtils::xReqIdCommand, SmtpCommand::Type::NonTransactionCommand);
      updateBytesMeterOnCommand(data);
      callbacks_->sendUpstream(data);
      setTransactionState(SmtpTransaction::State::XReqIdTransfer);
      result = SmtpUtils::Result::Stopped;
      return result;
    }
  } else {
    // error response to mail command
    setTransactionState(SmtpTransaction::State::None);
  }

  return result;
}

SmtpUtils::Result SmtpSession::handleXReqIdResponse(uint16_t& response_code,
                                                    std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, response_code);
  if (transaction_in_progress_) {
    updateBytesMeterOnResponse(callbacks_->getWriteBuffer());
    setTransactionState(SmtpTransaction::State::TransactionInProgress);
    getTransaction()->setXReqIdSent(true);
  } else {
    x_req_id_sent_ = true;
    setState(SmtpSession::State::ConnectionSuccess);
  }

  callbacks_->sendReplyDownstream(getResponseOnHold());
  result = SmtpUtils::Result::Stopped;
  return result;
}

SmtpUtils::Result SmtpSession::handleRcptResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (response_code == 250 || response_code == 251) {
    setTransactionState(SmtpTransaction::State::TransactionInProgress);
  } else if (response_code >= 400 && response_code <= 599) {
    callbacks_->incMailRcptErrors();
  }
  storeResponse(response, response_code);
  updateBytesMeterOnResponse(callbacks_->getWriteBuffer());
  return result;
}

SmtpUtils::Result SmtpSession::handleDataResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, response_code);
  updateBytesMeterOnResponse(callbacks_->getWriteBuffer());
  if (response_code == 250) {
    setTransactionState(SmtpTransaction::State::TransactionCompleted);
    getTransaction()->setStatus(SmtpUtils::statusSuccess);
    getSessionStats().transactions_completed++;
  } else if (response_code == 354) {
    // Intermediate response.
    return result;
  } else if (response_code >= 400 && response_code <= 599) {
    callbacks_->incMailDataTransferErrors();
    setTransactionState(SmtpTransaction::State::None);
    getTransaction()->setStatus(SmtpUtils::statusFailed);
    getSessionStats().transactions_failed++;
  }
  onTransactionComplete();
  setDataTransferInProgress(false);
  return result;
}

SmtpUtils::Result SmtpSession::handleResetResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, response_code);

  if (transaction_in_progress_) {
    if (response_code == 250) {
      abortTransaction();
    } else {
      setTransactionState(SmtpTransaction::State::TransactionInProgress);
    }
  }

  return result;
}

SmtpUtils::Result SmtpSession::handleQuitResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, response_code);
  if (response_code == 221) {
    terminateSession();
  } else {
    setState(SmtpSession::State::SessionInProgress);
  }
  return result;
}

SmtpUtils::Result SmtpSession::handleAuthResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  storeResponse(response, response_code);
  if (response_code == 334) {
    return result;
  } else if (response_code >= 400 && response_code <= 599) {
    callbacks_->incSmtpAuthErrors();
  } else if (response_code >= 200 && response_code <= 299) {
    setAuthStatus(true);
  }
  setState(SmtpSession::State::SessionInProgress);
  return result;
}

SmtpUtils::Result SmtpSession::handleStarttlsResponse(uint16_t& response_code,
                                                      std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (getState() == SmtpSession::State::UpstreamTlsNegotiation) {
    if (response_code == 220) {
      if (callbacks_->upstreamStartTls()) {
        // Upstream TLS connection established.Now encrypt downstream connection.
        upstream_session_type_ = SmtpUtils::SessionType::Tls;
        handleDownstreamTls();
        result = SmtpUtils::Result::Stopped;
        return result;
      }
    }
    // We terminate this session if upstream server does not support TLS i.e. response code != 220
    // or if TLS handshake error occured with upstream
    storeResponse(response, response_code);
    terminateSession();
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, "TLS not supported"));
    callbacks_->closeDownstreamConnection();
    result = SmtpUtils::Result::Stopped;
    return result;
  }
  storeResponse(response, response_code);
  return result;
}

SmtpUtils::Result SmtpSession::handleOtherResponse(uint16_t& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  storeResponse(response, response_code);
  return result;
}

SmtpUtils::Result SmtpSession::storeResponse(std::string response, uint16_t response_code) {

  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (current_command_ == nullptr)
    return result;

  // current_command_->storeResponse(response, response_code);
  current_command_->onComplete(response, response_code);

  if (response_code / 100 == 3) {
    // If intermediate response code, do not end current command processing
    return result;
  }

  command_in_progress_ = false;
  session_stats_.total_commands++;
  switch (current_command_->getType()) {
  case SmtpCommand::Type::NonTransactionCommand: {
    session_commands_.push_back(current_command_);
    break;
  }
  case SmtpCommand::Type::TransactionCommand: {
    getTransaction()->addTrxnCommand(current_command_);
    break;
  }
  default:
    break;
  };
  return result;
}

void SmtpSession::encode(ProtobufWkt::Struct& metadata) {

  auto& fields = *(metadata.mutable_fields());

  // TODO: store total number of transaction and commands

  ProtobufWkt::Value total_transactions;
  total_transactions.set_number_value(session_stats_.total_transactions);
  fields["total_transactions"] = total_transactions;

  ProtobufWkt::Value transactions_completed;
  transactions_completed.set_number_value(session_stats_.transactions_completed);
  fields["transactions_completed"] = transactions_completed;

  ProtobufWkt::Value transactions_failed;
  transactions_failed.set_number_value(session_stats_.transactions_failed);
  fields["transactions_failed"] = transactions_failed;

  ProtobufWkt::Value transactions_aborted;
  transactions_aborted.set_number_value(session_stats_.transactions_aborted);
  fields["transactions_aborted"] = transactions_aborted;

  ProtobufWkt::Value total_commands;
  total_commands.set_number_value(session_stats_.total_commands);
  fields["total_commands"] = total_commands;

  ProtobufWkt::Value upstream_session_type;
  if (upstream_session_type_ == SmtpUtils::SessionType::PlainText) {
    upstream_session_type.set_string_value("PlainText");
  } else {
    upstream_session_type.set_string_value("TLS");
  }
  fields["upstream_session_type"] = upstream_session_type;
}

void SmtpSession::setSessionMetadata() {
  StreamInfo::StreamInfo& parent_stream_info = callbacks_->getStreamInfo();
  ProtobufWkt::Struct metadata(
      (*parent_stream_info.dynamicMetadata()
            .mutable_filter_metadata())[NetworkFilterNames::get().SmtpProxy]);

  auto& fields = *metadata.mutable_fields();
  // Emit SMTP session metadata
  ProtobufWkt::Struct session_metadata;
  encode(session_metadata);
  fields["session_metadata"].mutable_struct_value()->CopyFrom(session_metadata);

  ProtobufWkt::Value session_id;
  session_id.set_string_value(session_id_);
  fields["session_id"] = session_id;

  parent_stream_info.setDynamicMetadata(NetworkFilterNames::get().SmtpProxy, metadata);
  // callbacks_->emitLogEntry(parent_stream_info);
}

void SmtpSession::terminateSession() {
  setState(SmtpSession::State::SessionTerminated);
  callbacks_->incSmtpSessionsCompleted();
  if (transaction_in_progress_) {
    abortTransaction();
  }
  setSessionMetadata();
}

void SmtpSession::abortTransaction() {
  callbacks_->incSmtpTransactionsAborted();
  getTransaction()->setStatus(SmtpUtils::statusAborted);
  getSessionStats().transactions_aborted++;
  onTransactionComplete();
}

void SmtpSession::onTransactionComplete() {

  callbacks_->incSmtpTransactions();
  session_stats_.total_transactions++;
  transaction_in_progress_ = false;
  getTransaction()->onComplete();
  getTransaction()->emitLog();
  endTransaction();
}

void SmtpSession::endTransaction() {
  if (smtp_transaction_ != nullptr) {
    delete smtp_transaction_;
    smtp_transaction_ = nullptr;
  }
}

void SmtpSession::handleDownstreamTls() {
  setState(SmtpSession::State::DownstreamTlsNegotiation);
  if (!callbacks_->downstreamStartTls(
          SmtpUtils::generateResponse(220, {2, 0, 0}, "Ready to start TLS"))) {
    // callback returns false if connection is switched to tls i.e. tls termination is
    // successful.
    callbacks_->incTlsTerminatedSessions();
    setSessionEncrypted(true);
    setState(SmtpSession::State::SessionInProgress);
  } else {
    // error while switching transport socket to tls.
    callbacks_->incTlsTerminationErrors();
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(550, {5, 0, 0}, "TLS Handshake error"));
    terminateSession();
    callbacks_->closeDownstreamConnection();
  }
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
