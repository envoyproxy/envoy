#pragma once
#include <cstdint>
#include "contrib/smtp_proxy/filters/network/source/smtp_transaction.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpSession {
public:
  enum class State {
    CONNECTION_REQUEST = 0,
    CONNECTION_SUCCESS = 1,
    SESSION_INIT_REQUEST = 2,
    SESSION_IN_PROGRESS = 3,
    SESSION_TERMINATION_REQUEST = 4,
    SESSION_TERMINATED = 5,
    UPSTREAM_TLS_NEGOTIATION = 6,
    DOWNSTREAM_TLS_NEGOTIATION = 7,
    SESSION_AUTH_REQUEST = 8,
  };

  void setState(SmtpSession::State state) { state_ = state; }
  SmtpSession::State getState() { return state_; }

  void SetTransactionState(SmtpTransaction::State state) { smtp_transaction_.setState(state); };
  SmtpTransaction::State getTransactionState() { return smtp_transaction_.getState(); }

private:
  SmtpSession::State state_{State::CONNECTION_REQUEST};
  SmtpTransaction smtp_transaction_{};
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
