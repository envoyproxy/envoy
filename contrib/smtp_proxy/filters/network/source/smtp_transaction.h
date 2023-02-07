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
    RCPT_COMMAND = 6,
    TRANSACTION_COMPLETED = 7,
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
