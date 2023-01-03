
#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"
#include "source/common/common/logger.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

Decoder::Result DecoderImpl::onData(Buffer::Instance& data, bool upstream) {
  Decoder::Result result = Decoder::Result::ReadyForNext;
  if (upstream) {
    result = parseResponse(data);
  } else {
    result = parseCommand(data);
  }
  data.drain(data.length());
  return result;
}

Decoder::Result DecoderImpl::parseCommand(Buffer::Instance& data) {
  ENVOY_LOG(debug, "smtp_proxy: decoding {} bytes", data.length());

  std::string command = data.toString();

  if (command.length() < 6) {
    // Message size is not sufficient to parse. Minimum 6 bytes required (4 byte command verb + CRLF
    // \r\n character sequence)
    return Decoder::Result::ReadyForNext;
  }

  switch (session_.getState()) {
  case SmtpSession::State::CONNECTION_SUCCESS: {
    if (absl::StartsWithIgnoreCase(command, smtpEhloCommand) ||
        absl::StartsWithIgnoreCase(command, smtpHeloCommand)) {
      session_.setState(SmtpSession::State::SESSION_INIT_REQUEST);
    }
    break;
  }
  case SmtpSession::State::SESSION_IN_PROGRESS: {

    if (absl::StartsWithIgnoreCase(command, startTlsCommand)) {
      if (session_encrypted_) {
        ENVOY_LOG(error, "smtp_proxy: received starttls when session is already encrypted.");
        callbacks_->sendReplyDownstream(outOfOrderCommandResponse);
        return Decoder::Result::Stopped;
      }

      if (!callbacks_->onStartTlsCommand(readyToStartTlsResponse)) {
        // callback returns false if connection is switched to tls i.e. tls termination is
        // successful.
        session_encrypted_ = true;
      } else {
        // error while switching transport socket to tls.
        callbacks_->sendReplyDownstream(failedToStartTlsResponse);
      }
      return Decoder::Result::Stopped;

    } else if (absl::StartsWithIgnoreCase(command, smtpQuitCommand)) {
      session_.setState(SmtpSession::State::SESSION_TERMINATION_REQUEST);
      break;
    } else if (absl::StartsWithIgnoreCase(command, smtpEhloCommand) ||
               absl::StartsWithIgnoreCase(command, smtpHeloCommand)) {
      session_.setState(SmtpSession::State::SESSION_INIT_REQUEST);
      break;
    }

    switch (session_.getTransactionState()) {
    case SmtpTransaction::State::NONE:
    case SmtpTransaction::State::TRANSACTION_COMPLETED: {

      if (absl::StartsWithIgnoreCase(command, smtpMailCommand)) {
        session_.SetTransactionState(SmtpTransaction::State::TRANSACTION_REQUEST);
      }
      break;
    }
    case SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST:
    case SmtpTransaction::State::TRANSACTION_IN_PROGRESS: {

      if (absl::StartsWithIgnoreCase(command, smtpDataCommand)) {
        session_.SetTransactionState(SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST);
      } else if (absl::StartsWithIgnoreCase(command, smtpRsetCommand) ||
                 absl::StartsWithIgnoreCase(command, smtpEhloCommand) ||
                 absl::StartsWithIgnoreCase(command, smtpHeloCommand)) {
        session_.SetTransactionState(SmtpTransaction::State::TRANSACTION_ABORT_REQUEST);
      }
      break;
    }
    default:
      break;
    }
    break;
  } // End case SESSION_IN_PROGRESS
  default:
    break;
  }
  return Decoder::Result::ReadyForNext;
}

/*
  Response from upstream server is processed as per Command-Reply Sequence given in
  rfc https://www.rfc-editor.org/rfc/rfc5321 section: 4.3.2.
  We set Session and transaction states based on response received.
*/
Decoder::Result DecoderImpl::parseResponse(Buffer::Instance& data) {
  ENVOY_LOG(debug, "smtp_proxy: decoding response {} bytes", data.length());

  Decoder::Result result = Decoder::Result::ReadyForNext;

  if (data.length() < 3) {
    // Minimum 3 byte response code needed to parse response from server.
    return result;
  }
  std::string response;
  response.assign(std::string(static_cast<char*>(data.linearize(3)), 3));

  uint16_t response_code = stoi(response);
  switch (session_.getState()) {

  case SmtpSession::State::CONNECTION_REQUEST: {
    if (response_code == 220) {
      session_.setState(SmtpSession::State::CONNECTION_SUCCESS);
    } else if (response_code == 554) {
      callbacks_->incSmtpConnectionEstablishmentErrors();
    }
    break;
  }

  case SmtpSession::State::SESSION_INIT_REQUEST: {
    if (response_code == 250) {
      session_.setState(SmtpSession::State::SESSION_IN_PROGRESS);
      if (session_.getTransactionState() == SmtpTransaction::State::TRANSACTION_IN_PROGRESS ||
          session_.getTransactionState() == SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST) {
        // Increment stats for icomplete transactions when session is abruptly terminated.
        callbacks_->incSmtpTransactionsAborted();
        session_.SetTransactionState(SmtpTransaction::State::NONE);
      }
    }
    break;
  }

  case SmtpSession::State::SESSION_IN_PROGRESS: {

    switch (session_.getTransactionState()) {
    case SmtpTransaction::State::TRANSACTION_REQUEST: {
      if (response_code == 250) {
        session_.SetTransactionState(SmtpTransaction::State::TRANSACTION_IN_PROGRESS);
      }
      break;
    }
    case SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST: {
      if (response_code == 250) {
        callbacks_->incSmtpTransactions();
        session_.SetTransactionState(SmtpTransaction::State::TRANSACTION_COMPLETED);
      }
      break;
    }
    case SmtpTransaction::State::TRANSACTION_ABORT_REQUEST: {
      if (response_code == 250) {
        callbacks_->incSmtpTransactionsAborted();
        session_.SetTransactionState(SmtpTransaction::State::NONE);
      }
      break;
    }
    default:
      break;
    }
    break;
  }
  case SmtpSession::State::SESSION_TERMINATION_REQUEST: {
    if (response_code == 221) {
      session_.setState(SmtpSession::State::SESSION_TERMINATED);
      callbacks_->incSmtpSessionsCompleted();
      if (session_.getTransactionState() == SmtpTransaction::State::TRANSACTION_IN_PROGRESS ||
          session_.getTransactionState() == SmtpTransaction::State::MAIL_DATA_TRANSFER_REQUEST) {
        // Increment stats for icomplete transactions when session is abruptly terminated.
        callbacks_->incSmtpTransactionsAborted();
      }
    }
    break;
  }
  default:
    result = Decoder::Result::ReadyForNext;
  }
  if (response_code >= 400 && response_code <= 499) {
    callbacks_->incSmtp4xxErrors();
  } else if (response_code >= 500 && response_code <= 599) {
    callbacks_->incSmtp5xxErrors();
  }
  return result;
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy