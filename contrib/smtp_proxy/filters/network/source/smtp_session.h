#pragma once
#include <cstdint>

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// Class stores data about the current state of a transaction between SMTP client and server.

class SmtpTransaction {
public:
  enum class State {
    NONE = 0,
    TRANSACTION_REQUEST = 1,
    TRANSACTION_IN_PROGRESS = 2,
    TRANSACTION_ABORT_REQUEST = 3,
    TRANSACTION_ABORTED = 4,
    MAIL_DATA_TRANSFER_REQUEST = 5,
    TRANSACTION_COMPLETED = 6,
  };
  void setState(SmtpTransaction::State state) { state_ = state; }
  SmtpTransaction::State getState() { return state_; }

private:
  SmtpTransaction::State state_{State::NONE};
};

class SmtpSession {
public:
  enum class State {
    CONNECTION_REQUEST = 0,
    CONNECTION_SUCCESS = 1,
    SESSION_INIT_REQUEST = 2,
    SESSION_IN_PROGRESS = 3,
    SESSION_TERMINATION_REQUEST = 4,
    SESSION_TERMINATED = 5,
  };

  void setState(SmtpSession::State state) { state_ = state; }
  SmtpSession::State getState() { return state_; }
  bool inTransaction() { return in_transaction_; };
  void setInTransaction(bool in_transaction) { in_transaction_ = in_transaction; };

  void SetTransactionState(SmtpTransaction::State state) { smtp_transaction_.setState(state); };
  SmtpTransaction::State getTransactionState() { return smtp_transaction_.getState(); }

private:
  bool in_transaction_{false};
  SmtpSession::State state_{State::CONNECTION_REQUEST};
  SmtpTransaction smtp_transaction_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
