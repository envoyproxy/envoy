#pragma once
#include <cstdint>

#include "source/common/common/logger.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_transaction.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpSession {
public:
  enum class State {
    ConnectionRequest = 0,
    ConnectionSuccess = 1,
    SessionInitRequest = 2,
    SessionInProgress = 3,
    SessionTerminationRequest = 4,
    SessionTerminated = 5,
    UpstreamTlsNegotiation = 6,
    DownstreamTlsNegotiation = 7,
    SessionAuthRequest = 8,
  };

  void setState(SmtpSession::State state) { state_ = state; }
  SmtpSession::State getState() { return state_; }

  void setTransactionState(SmtpTransaction::State state) { smtp_transaction_.setState(state); };
  SmtpTransaction::State getTransactionState() { return smtp_transaction_.getState(); }

  void setSessionEncrypted(bool flag) { session_encrypted_ = flag; }
  bool isSessionEncrypted() const { return session_encrypted_; }

private:
  SmtpSession::State state_{State::ConnectionRequest};
  SmtpTransaction smtp_transaction_{};
  bool session_encrypted_{false}; // tells if exchange is encrypted
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
