#pragma once
#include <cstdint>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// Class stores data about the current state of a transaction between SMTP client and server.

class SmtpTransaction {
public:
  enum class State {
    NONE = 0,
    TransactionRequest = 1,
    TransactionInProgress = 2,
    TransactionAbortRequest = 3,
    TransactionAborted = 4,
    MailDataTransferRequest = 5,
    RcptCommand = 6,
    TransactionCompleted = 7,
  };

  void setState(SmtpTransaction::State state) { state_ = state; }
  SmtpTransaction::State getState() { return state_; }

private:
  SmtpTransaction::State state_{State::NONE};
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
