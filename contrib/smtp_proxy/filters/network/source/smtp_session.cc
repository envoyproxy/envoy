#include "contrib/smtp_proxy/filters/network/source/smtp_session.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stats/timespan_impl.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

SmtpSession::SmtpSession(DecoderCallbacks* callbacks, TimeSource& time_source,
                         Random::RandomGenerator& random_generator)
    : callbacks_(callbacks), time_source_(time_source), random_generator_(random_generator) {
  newCommand(StringUtil::toUpper("connect_req"), SmtpCommand::Type::NonTransactionCommand);
  session_length_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      callbacks_->getStats().session_length_, time_source_);
  callbacks_->incActiveSession();
}

void SmtpSession::newCommand(const std::string& name, SmtpCommand::Type type) {
  current_command_ = std::make_shared<SmtpCommand>(name, type, time_source_);
  command_in_progress_ = true;
  command_length_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
      callbacks_->getStats().command_length_, time_source_);
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

  if (transaction_in_progress_ && getTransaction() != nullptr) {
    msg_ = SmtpUtils::duplicate_mail_from;
    getTransaction()->setStatus(SmtpUtils::passthroughMode);
    getTransaction()->setMsg(SmtpUtils::duplicate_mail_from);
    callbacks_->getStats().duplicate_cmd_.inc();
    return SmtpUtils::Result::ProtocolError;
  }

  if (state_ != SmtpSession::State::SessionInProgress) {
    std::string resp_msg = SmtpUtils::generateResponse(
        502, {5, 5, 1}, "Please introduce yourself first with EHLO/HELO");
    setSessionStatus(SmtpUtils::missing_ehlo, SmtpUtils::local_5xx_error);
    recordLocalRespInSession(SmtpUtils::smtpMailCommand, 502, resp_msg, SmtpUtils::local_5xx_error);
    callbacks_->sendReplyDownstream(resp_msg);
    callbacks_->getStats().local_5xx_errors_.inc();
    callbacks_->getStats().local_5xx_missing_ehlo_.inc();
    callbacks_->getStats().bad_cmd_sequence_.inc();
    return SmtpUtils::Result::Stopped;
  }

  if (arg.length() < 6 || !absl::EqualsIgnoreCase(arg.substr(0, 5), "FROM:")) {
    // MAIL FROM command not in proper syntax, enter into passthrough mode.
    msg_ = "bad_mail_from_cmd_syntax";
    callbacks_->getStats().protocol_parse_error_.inc();
    return SmtpUtils::Result::ProtocolError;
  }

  // If STARTTLS is required for this session and client has not issued it earlier, abort the
  // session.
  if (callbacks_->downstreamTlsRequired() && !isSessionEncrypted()) {
    callbacks_->getStats().mail_req_rejected_due_to_non_tls_.inc();
    std::string resp_msg =
        SmtpUtils::generateResponse(530, {5, 7, 10},
                                    "plain-text connection is not allowed for this server. Please "
                                    "upgrade the connection to TLS");
    recordLocalRespInSession(SmtpUtils::smtpMailCommand, 530, resp_msg, SmtpUtils::local_5xx_error);
    callbacks_->sendReplyDownstream(resp_msg);
    terminateSession(SmtpUtils::plaintext_trxn_not_allowed, SmtpUtils::statusError);
    callbacks_->closeDownstreamConnection();
    return SmtpUtils::Result::Stopped;
  }
  newCommand(SmtpUtils::smtpMailCommand, SmtpCommand::Type::TransactionCommand);
  createNewTransaction();
  std::string sender = SmtpUtils::extractAddress(arg);
  getTransaction()->setSender(sender);
  callbacks_->incSmtpTransactionRequests();
  updateBytesMeterOnCommand(callbacks_->getReadBuffer());
  setTransactionState(SmtpTransaction::State::TransactionRequest);
  return SmtpUtils::Result::ReadyForNext;
}

SmtpUtils::Result SmtpSession::handleRcpt(std::string& arg) {

  if (!transaction_in_progress_) {
    std::string resp_msg = SmtpUtils::generateResponse(502, {5, 5, 1}, "Missing MAIL FROM command");
    setSessionStatus(SmtpUtils::missing_mail_from_cmd, SmtpUtils::local_5xx_error);
    recordLocalRespInSession(SmtpUtils::smtpRcptCommand, 502, resp_msg, SmtpUtils::local_5xx_error);
    callbacks_->sendReplyDownstream(resp_msg);
    callbacks_->getStats().local_5xx_errors_.inc();
    callbacks_->getStats().local_5xx_missing_mail_.inc();
    callbacks_->getStats().bad_cmd_sequence_.inc();
    return SmtpUtils::Result::Stopped;
  }

  if (arg.length() <= 3 || !absl::EqualsIgnoreCase(arg.substr(0, 3), "TO:")) {
    msg_ = "bad_rcpt_cmd_syntax";
    callbacks_->getStats().protocol_parse_error_.inc();
    return SmtpUtils::Result::ProtocolError;
  }
  newCommand(SmtpUtils::smtpRcptCommand, SmtpCommand::Type::TransactionCommand);
  std::string recipient = SmtpUtils::extractAddress(arg);
  getTransaction()->addRcpt(recipient);
  updateBytesMeterOnCommand(callbacks_->getReadBuffer());
  setTransactionState(SmtpTransaction::State::RcptCommand);
  return SmtpUtils::Result::ReadyForNext;
}

SmtpUtils::Result SmtpSession::handleData(std::string& arg) {

  if (arg.length() > 0) {
    msg_ = "bad_data_cmd_syntax";
    callbacks_->getStats().protocol_parse_error_.inc();
    return SmtpUtils::Result::ProtocolError;
  }

  if (!transaction_in_progress_ || getTransaction()->getNoOfRecipients() == 0) {
    std::string resp_msg = SmtpUtils::generateResponse(502, {5, 5, 1}, "Missing RCPT TO command");
    setSessionStatus( SmtpUtils::missing_rcpt_cmd, SmtpUtils::local_5xx_error);
    recordLocalRespInSession(SmtpUtils::smtpRcptCommand, 502, resp_msg, SmtpUtils::local_5xx_error);
    callbacks_->sendReplyDownstream(resp_msg);
    callbacks_->getStats().local_5xx_errors_.inc();
    callbacks_->getStats().local_5xx_missing_rcpt_.inc();
    callbacks_->getStats().bad_cmd_sequence_.inc();
    return SmtpUtils::Result::Stopped;
  }
  newCommand(SmtpUtils::smtpDataCommand, SmtpCommand::Type::TransactionCommand);
  updateBytesMeterOnCommand(callbacks_->getReadBuffer());
  setTransactionState(SmtpTransaction::State::MailDataTransferRequest);
  return SmtpUtils::Result::ReadyForNext;
}

SmtpUtils::Result SmtpSession::handleAuth() {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  newCommand(SmtpUtils::smtpAuthCommand, SmtpCommand::Type::NonTransactionCommand);
  if (isAuthenticated()) {
    setSessionStatus(SmtpUtils::duplicate_auth, SmtpUtils::local_5xx_error);
    callbacks_->getStats().local_5xx_duplilcate_auth_.inc();
    callbacks_->getStats().duplicate_cmd_.inc();
    current_command_->storeLocalResponse(SmtpUtils::duplicate_auth, "local", 502);
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, "Already authenticated"));
    result = SmtpUtils::Result::Stopped;
    return result;
  }
  setState(SmtpSession::State::SessionAuthRequest);
  return result;
}

SmtpUtils::Result SmtpSession::handleStarttls() {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  newCommand(SmtpUtils::startTlsCommand, SmtpCommand::Type::NonTransactionCommand);
  if (isSessionEncrypted()) {
    // record local response
    setSessionStatus(SmtpUtils::duplicate_starttls, SmtpUtils::local_5xx_error);
    callbacks_->getStats().local_5xx_duplilcate_starttls_.inc();
    current_command_->storeLocalResponse(SmtpUtils::tlsSessionActiveAlready, "local", 502);
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, SmtpUtils::tlsSessionActiveAlready));
    callbacks_->getStats().duplicate_cmd_.inc();
    result = SmtpUtils::Result::Stopped;
    return result;
  }

  if (callbacks_->downstreamTlsEnabled()) {
    if (callbacks_->upstreamTlsEnabled()) {
      // Forward STARTTLS request to upstream for TLS termination at upstream end.
      setState(SmtpSession::State::UpstreamTlsNegotiation);
    } else {
      // Perform downstream TLS termination.
      handleDownstreamTls();
      result = SmtpUtils::Result::Stopped;
    }
    return result;
  }
  // TODO below: handle case when client <--- plaintext ---> envoy <--- TLS ---> upstream ?

  if (callbacks_->upstreamTlsEnabled()) {
    // Forward STARTTLS request to upstream for TLS termination at upstream.
    setState(SmtpSession::State::UpstreamTlsNegotiation);
  }
  return result;
}

SmtpUtils::Result SmtpSession::handleReset(std::string& arg) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (arg.length() > 0) {
    msg_ = "bad_reset_cmd_syntax";
    callbacks_->getStats().protocol_parse_error_.inc();
    return SmtpUtils::Result::ProtocolError;
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
    msg_ = "bad_quit_cmd_syntax";
    callbacks_->getStats().protocol_parse_error_.inc();
    return SmtpUtils::Result::ProtocolError;
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

SmtpUtils::Result SmtpSession::handleResponse(int& response_code, std::string& response) {
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

SmtpUtils::Result SmtpSession::handleConnResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  if (response_code == 220) {
    setState(SmtpSession::State::ConnectionSuccess);
    auto stream_id_provider = callbacks_->getStreamInfo().getStreamIdProvider();
    if (stream_id_provider.has_value()) {
      session_id_ = stream_id_provider->toStringView().value_or("");
    }

    if (callbacks_->tracingEnabled() && !isXReqIdSent()) {
      response_on_hold_ =
          std::to_string(response_code) + " " + response + SmtpUtils::smtpCrlfSuffix;
      std::string x_req_id =
          std::string(SmtpUtils::xReqIdCommand) + " SESSION_ID=" + session_id_ + "\r\n";
      Buffer::OwnedImpl data(x_req_id);
      newCommand(SmtpUtils::xReqIdCommand, SmtpCommand::Type::NonTransactionCommand);
      callbacks_->sendUpstream(data);
      setState(SmtpSession::State::XReqIdTransfer);
      result = SmtpUtils::Result::Stopped;
      return result;
    }
  } else if (response_code >= 400 && response_code <= 599) {
    callbacks_->getStats().connection_establishment_errors_.inc();
    connect_resp_.response_code = response_code;
    connect_resp_.response_code_details = SmtpUtils::via_upstream;
    connect_resp_.response_str = response;
    setSessionStatus("upstream_cx_establishment_error", response);
  }
  return result;
}

SmtpUtils::Result SmtpSession::handleEhloResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  if (response_code == 250) {
    setState(SmtpSession::State::SessionInProgress);
    if (transaction_in_progress_) {
      // Abort this transaction.
      abortTransaction(SmtpUtils::abortedDueToEhlo);
    }
  } else {
    setSessionStatus("upstream_5xx_ehlo", response);
    callbacks_->getStats().upstream_5xx_ehlo_.inc();
    setState(SmtpSession::State::ConnectionSuccess);
  }

  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  return result;
}

SmtpUtils::Result SmtpSession::handleMailResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  updateBytesMeterOnResponse(callbacks_->getWriteBuffer());
  if (response_code == 250) {
    setTransactionState(SmtpTransaction::State::TransactionInProgress);
    callbacks_->incActiveTransaction();
    if (callbacks_->tracingEnabled() && !getTransaction()->isXReqIdSent()) {
      response_on_hold_ =
          std::to_string(response_code) + " " + response + SmtpUtils::smtpCrlfSuffix;
      std::string x_req_id = std::string(SmtpUtils::xReqIdCommand) +
                             " TRXN_ID=" + getTransaction()->getTransactionId() + "\r\n";
      Buffer::OwnedImpl data(x_req_id);
      newCommand(SmtpUtils::xReqIdCommand, SmtpCommand::Type::NonTransactionCommand);
      updateBytesMeterOnCommand(data);
      callbacks_->sendUpstream(data);
      setTransactionState(SmtpTransaction::State::XReqIdTransfer);
      result = SmtpUtils::Result::Stopped;
      return result;
    }
  } else if (response_code >= 400 && response_code <= 499) {
    // error response to mail command
    getTransaction()->setMsg(response);
    callbacks_->getStats().upstream_4xx_mail_from_.inc();
    getTransaction()->setStatus("upstream_4xx_mail_from");
    setTransactionState(SmtpTransaction::State::None);
  } else if (response_code >= 500 && response_code <= 599) {
    getTransaction()->setMsg(response);
    callbacks_->getStats().upstream_5xx_mail_from_.inc();
    getTransaction()->setStatus("upstream_5xx_mail_from");
    setTransactionState(SmtpTransaction::State::None);
  }

  return result;
}

SmtpUtils::Result SmtpSession::handleXReqIdResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  if (transaction_in_progress_) {
    // We sent trxn level x_req_id
    updateBytesMeterOnResponse(callbacks_->getWriteBuffer());
    setTransactionState(SmtpTransaction::State::TransactionInProgress);
    getTransaction()->setXReqIdSent(true);
  } else {
    // We sent session level x_req_id
    x_req_id_sent_ = true;
    setState(SmtpSession::State::ConnectionSuccess);
  }

  callbacks_->sendReplyDownstream(getResponseOnHold());
  result = SmtpUtils::Result::Stopped;
  return result;
}

SmtpUtils::Result SmtpSession::handleRcptResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  if (response_code == 250 || response_code == 251) {
    setTransactionState(SmtpTransaction::State::TransactionInProgress);
  } else if (response_code >= 400 && response_code <= 499) {
    // error response to rcpt command
    getTransaction()->setMsg(response);
    callbacks_->getStats().upstream_4xx_rcpt_.inc();
    getTransaction()->setStatus("upstream_4xx_rcpt");
  } else if (response_code >= 500 && response_code <= 599) {
    getTransaction()->setMsg(response);
    callbacks_->getStats().upstream_5xx_rcpt_.inc();
    getTransaction()->setStatus("upstream_5xx_rcpt");
  }

  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  updateBytesMeterOnResponse(callbacks_->getWriteBuffer());
  return result;
}

SmtpUtils::Result SmtpSession::handleDataResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  updateBytesMeterOnResponse(callbacks_->getWriteBuffer());
  if (response_code == 250) {
    setTransactionState(SmtpTransaction::State::TransactionCompleted);
    getTransaction()->setStatus(SmtpUtils::statusSuccess);
    onTransactionComplete();
  } else if (response_code == 354) {
    // Intermediate response.
    setDataTransferInProgress(true);
    data_tx_length_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(
        callbacks_->getStats().data_tx_length_, time_source_);

    return result;
  } else if (response_code >= 400 && response_code <= 499) {
    // error response to data command
    getTransaction()->setMsg(response);
    callbacks_->getStats().upstream_4xx_data_.inc();
    getTransaction()->setStatus("upstream_4xx_data");
    setTransactionState(SmtpTransaction::State::None);
    onTransactionFailed(response);
  } else if (response_code >= 500 && response_code <= 599) {
    getTransaction()->setMsg(response);
    callbacks_->getStats().upstream_5xx_data_.inc();
    getTransaction()->setStatus("upstream_5xx_data");
    setTransactionState(SmtpTransaction::State::None);
    onTransactionFailed(response);
  }

  if (data_tx_length_ != nullptr) {
    data_tx_length_->complete();
  }
  setDataTransferInProgress(false);
  return result;
}

SmtpUtils::Result SmtpSession::handleResetResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);

  if (transaction_in_progress_) {
    if (response_code == 250) {
      abortTransaction(SmtpUtils::abortedDueToRset);
    } else {
      setTransactionState(SmtpTransaction::State::TransactionInProgress);
    }
  }

  return result;
}

SmtpUtils::Result SmtpSession::handleQuitResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;
  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  if (response_code == 221) {
    onSessionComplete();
  } else {
    setState(SmtpSession::State::SessionInProgress);
  }
  return result;
}

SmtpUtils::Result SmtpSession::handleAuthResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  if (response_code == 334) {
    return result;
  } else if (response_code >= 400 && response_code <= 499) {
    callbacks_->getStats().upstream_4xx_auth_.inc();
    setSessionStatus("upstream_4xx_auth", response);
  }  else if (response_code >= 500 && response_code <= 599) {
    callbacks_->getStats().upstream_5xx_auth_.inc();
    setSessionStatus("upstream_5xx_auth", response);
  } else if (response_code >= 200 && response_code <= 299) {
    setAuthStatus(true);
  }
  setState(SmtpSession::State::SessionInProgress);
  return result;
}

SmtpUtils::Result SmtpSession::handleStarttlsResponse(int& response_code, std::string& response) {
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
    storeResponse(response, SmtpUtils::via_upstream, response_code,
                  SmtpCommand::ResponseType::ViaUpstream);
    terminateSession(SmtpUtils::upstream_tls_error, SmtpUtils::statusFailed);
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(502, {5, 5, 1}, "TLS not supported"));
    callbacks_->closeDownstreamConnection();
    result = SmtpUtils::Result::Stopped;
    return result;
  }
  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  return result;
}

SmtpUtils::Result SmtpSession::handleOtherResponse(int& response_code, std::string& response) {
  SmtpUtils::Result result = SmtpUtils::Result::ReadyForNext;

  storeResponse(response, SmtpUtils::via_upstream, response_code,
                SmtpCommand::ResponseType::ViaUpstream);
  return result;
}

void SmtpSession::recordLocalRespInSession(const std::string& command, int resp_code,
                                           std::string msg, std::string resp_code_details) {
  std::shared_ptr<SmtpCommand> cmd =
      std::make_shared<SmtpCommand>(command, resp_code, msg, resp_code_details, time_source_);
  session_commands_.push_back(cmd);
  error_resp_code_ = resp_code;
  error_resp_ = msg;
  error_resp_code_details_ = resp_code_details;
}

void SmtpSession::setSessionStatus(const std::string& status, const std::string& msg) {
  status_ = status;
  msg_ = msg;
}

SmtpUtils::Result SmtpSession::storeResponse(std::string response, std::string resp_code_details,
                                             int response_code,
                                             SmtpCommand::ResponseType resp_type) {

  if (current_command_ == nullptr)
    return SmtpUtils::Result::ReadyForNext;

  current_command_->onComplete(response, resp_code_details, response_code, resp_type);

  if (response_code >= 400 && response_code < 500) {
    if (current_command_->isLocalResponseSet()) {
      callbacks_->getStats().local_4xx_errors_.inc();
    } else {
      callbacks_->getStats().upstream_4xx_errors_.inc();
    }
  } else if (response_code >= 500 && response_code <= 599) {
    if (current_command_->isLocalResponseSet()) {
      callbacks_->getStats().local_5xx_errors_.inc();
    } else {
      callbacks_->getStats().upstream_5xx_errors_.inc();
    }
  }

  if (response_code / 100 == 3) {
    if (current_command_->getName() == SmtpUtils::smtpDataCommand) {
      command_length_->complete();
    }
    // If intermediate response code, do not end current command processing
    return SmtpUtils::Result::ReadyForNext;
  }

  if (current_command_->getName() != SmtpUtils::smtpDataCommand) {
    command_length_->complete();
  }
  command_in_progress_ = false;
  session_stats_.total_commands++;
  switch (current_command_->getType()) {
  case SmtpCommand::Type::NonTransactionCommand: {
    session_commands_.push_back(current_command_);
    if (response_code >= 400 && response_code <= 599) {
      error_resp_code_ = response_code;
      error_resp_ = response;
      error_resp_code_details_ = resp_code_details;
    }
    break;
  }
  case SmtpCommand::Type::TransactionCommand: {
    if (getTransaction()) {
      getTransaction()->addTrxnCommand(current_command_);
      if (response_code >= 400 && response_code <= 599) {
        getTransaction()->setErrRespCode(response_code);
        getTransaction()->setErrResponse(response);
        getTransaction()->setErrRespCodeDetails(resp_code_details);
      }
    }

    break;
  }
  default:
    break;
  };
  return SmtpUtils::Result::ReadyForNext;
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

  ProtobufWkt::ListValue commands;
  for (auto command : session_commands_) {
    ProtobufWkt::Struct data_struct;
    auto& fields = *(data_struct.mutable_fields());

    ProtobufWkt::Value name;
    name.set_string_value(command->getName());
    fields["command_verb"] = name;

    ProtobufWkt::Value response_code;
    response_code.set_number_value(command->getResponseCode());
    fields["response_code"] = response_code;

    ProtobufWkt::Value response_code_details;
    response_code_details.set_string_value(command->getResponseCodeDetails());
    fields["response_code_details"] = response_code_details;

    ProtobufWkt::Value response_msg;
    response_msg.set_string_value(command->getResponseMsg());
    fields["msg"] = response_msg;

    ProtobufWkt::Value duration;
    duration.set_number_value(command->getDuration());
    fields["duration"] = duration;

    commands.add_values()->mutable_struct_value()->CopyFrom(data_struct);
  }
  fields["commands"].mutable_list_value()->CopyFrom(commands);
}

void SmtpSession::setSessionMetadata() {
  StreamInfo::StreamInfo& parent_stream_info = callbacks_->getStreamInfo();
  // std::string response_flags = StreamInfo::ResponseFlagUtils::toShortString(parent_stream_info);
  // std::cout << "response flags: " << response_flags << std::endl;
  // if (parent_stream_info.upstreamInfo()) {
  //   std::string upstream_failure_reason =
  //       parent_stream_info.upstreamInfo()->upstreamTransportFailureReason();
  //   std::cout << "upstream_failure_reason: " << upstream_failure_reason << std::endl;
  // }
  // std::shared_ptr<StreamInfo::UpstreamInfo> upstream_info = parent_stream_info.upstreamInfo();
  // if (upstream_info) {
  //   int conn_id = upstream_info->upstreamConnectionId().value_or(-1);
  //   std::cout << "upstream connection id:" << conn_id << std::endl;
  // }

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

  ProtobufWkt::Value log_type;
  log_type.set_string_value("session");
  fields["type"] = log_type;

  ProtobufWkt::Value status;
  status.set_string_value(status_);
  fields["status"] = status;

  ProtobufWkt::Value msg;
  msg.set_string_value(msg_);
  fields["msg"] = msg;

  ProtobufWkt::Value err_resp;
  err_resp.set_string_value(error_resp_);
  fields["error_response"] = err_resp;

  ProtobufWkt::Value err_resp_code_details;
  err_resp_code_details.set_string_value(error_resp_code_details_);
  fields["error_resp_code_details"] = err_resp_code_details;

  ProtobufWkt::Value err_resp_code;
  err_resp_code.set_number_value(error_resp_code_);
  fields["error_resp_code"] = err_resp_code;

  parent_stream_info.setDynamicMetadata(NetworkFilterNames::get().SmtpProxy, metadata);
}

void SmtpSession::terminateSession(std::string status, std::string msg) {
  status_ = status;
  msg_ = msg;
  callbacks_->incSmtpSessionsTerminated();
  callbacks_->decActiveSession();
  setState(SmtpSession::State::SessionTerminated);
  if (transaction_in_progress_) {
    abortTransaction(SmtpUtils::trxnAbortedDueToSessionClose);
  }
  setSessionMetadata();
  session_length_->complete();
}

void SmtpSession::onSessionComplete() {
  if (status_.length() == 0) {
    status_ = SmtpUtils::statusSuccess;
  }
  callbacks_->incSmtpSessionsCompleted();
  callbacks_->decActiveSession();
  setState(SmtpSession::State::SessionTerminated);
  if (transaction_in_progress_) {
    abortTransaction(SmtpUtils::trxnAbortedDueToSessionClose);
  }
  setSessionMetadata();
  session_length_->complete();
}

// void SmtpSession::endSession() {
//   callbacks_->decActiveSession();
//   setState(SmtpSession::State::SessionTerminated);

//   if (transaction_in_progress_) {
//     abortTransaction();
//   }
//   setSessionMetadata();
//   session_length_->complete();
// }

void SmtpSession::abortTransaction(std::string reason) {
  callbacks_->incSmtpTransactionsAborted();
  getTransaction()->setMsg(reason);
  getTransaction()->setStatus(SmtpUtils::statusAborted);
  getSessionStats().transactions_aborted++;
  endTransaction();
}

void SmtpSession::onTransactionComplete() {
  callbacks_->incSmtpTransactionsCompleted();
  getTransaction()->setStatus(SmtpUtils::statusSuccess);
  getSessionStats().transactions_completed++;
  endTransaction();
}

void SmtpSession::onTransactionFailed(std::string& response) {
  getTransaction()->setStatus(SmtpUtils::statusFailed);
  getTransaction()->setMsg(response);
  getSessionStats().transactions_failed++;
  callbacks_->incSmtpTrxnFailed();
  endTransaction();
}

void SmtpSession::endTransaction() {

  // Emit access log for this transaction using transaction stream_info.
  session_stats_.total_transactions++;
  transaction_in_progress_ = false;
  callbacks_->decActiveTransaction();
  getTransaction()->onComplete();
  getTransaction()->emitLog();

  if (smtp_transaction_ != nullptr) {
    delete smtp_transaction_;
    smtp_transaction_ = nullptr;
  }
}

void SmtpSession::handleDownstreamTls() {
  setState(SmtpSession::State::DownstreamTlsNegotiation);
  getCurrentCommand()->storeLocalResponse("Ready to start TLS", "local", 220);
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
    getCurrentCommand()->storeLocalResponse("TLS Handshake error", "local", 550);
    callbacks_->sendReplyDownstream(
        SmtpUtils::generateResponse(550, {5, 0, 0}, "TLS Handshake error"));
    terminateSession(SmtpUtils::downstream_tls_error, SmtpUtils::statusFailed);
    callbacks_->closeDownstreamConnection();
  }
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
