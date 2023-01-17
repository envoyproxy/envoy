#include "contrib/smtp_proxy/filters/network/source/smtp_transaction.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

void SmtpTransaction::parseCommand(std::string& command) {
  switch (state_) {
  case State::NONE:
  case State::TRANSACTION_COMPLETED: {

    if (absl::StartsWithIgnoreCase(command, SmtpUtils::smtpMailCommand)) {
      setState(State::TRANSACTION_REQUEST);
    }
    break;
  }
  case State::RCPT_COMMAND:
  case State::MAIL_DATA_TRANSFER_REQUEST:
  case State::TRANSACTION_IN_PROGRESS: {   

    if(absl::StartsWithIgnoreCase(command, SmtpUtils::smtpRcptCommand)) {
      setState(State::RCPT_COMMAND);
    } else if (absl::StartsWithIgnoreCase(command, SmtpUtils::smtpDataCommand)) {
      setState(State::MAIL_DATA_TRANSFER_REQUEST);
    } else if (absl::StartsWithIgnoreCase(command, SmtpUtils::smtpRsetCommand) ||
               absl::StartsWithIgnoreCase(command, SmtpUtils::smtpEhloCommand) ||
               absl::StartsWithIgnoreCase(command, SmtpUtils::smtpHeloCommand)) {
      setState(State::TRANSACTION_ABORT_REQUEST);
    }
    break;
  }
  default:
    break;
  }
}

void SmtpTransaction::parseResponse(uint16_t& response_code) {

  switch (state_) {
  case State::TRANSACTION_REQUEST: {
    if (response_code == 250) {
      setState(State::TRANSACTION_IN_PROGRESS);
    }
    break;
  }
  case State::RCPT_COMMAND: {
    if (response_code == 250 || response_code == 251) {
      setState(State::TRANSACTION_IN_PROGRESS);
    } else if(response_code >=400 && response_code <= 599) {
      callbacks_->incMailRcptErrors();
    }
    break;
  }
  case State::MAIL_DATA_TRANSFER_REQUEST: {
    if (response_code == 250) {
      setState(State::TRANSACTION_COMPLETED);
    } else if(response_code >=400 && response_code <= 599) {
      callbacks_->incMailDataTransferErrors();
      //Reset the transaction state in case of mail data transfer errors.
      setState(State::NONE);
    }
    callbacks_->incSmtpTransactions();
    break;
  }
  case State::TRANSACTION_ABORT_REQUEST: {
    if (response_code == 250) {
      callbacks_->incSmtpTransactionsAborted();
      setState(State::NONE);
    }
    break;
  }
  default:
    break;
  }
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
