#include "source/extensions/transport_sockets/tls/ssl_handshaker.h"

#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/transport_sockets/tls/utility.h"

using Envoy::Network::PostIoAction;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

void ValidateResultCallbackImpl::onSslHandshakeCancelled() { extended_socket_info_.reset(); }

void ValidateResultCallbackImpl::onCertValidationResult(bool succeeded,
                                                        Ssl::ClientValidationStatus detailed_status,
                                                        const std::string& /*error_details*/,
                                                        uint8_t tls_alert) {
  if (!extended_socket_info_.has_value()) {
    return;
  }
  extended_socket_info_->setCertificateValidationStatus(detailed_status);
  extended_socket_info_->setCertificateValidationAlert(tls_alert);
  extended_socket_info_->onCertificateValidationCompleted(succeeded, true);
}

SslExtendedSocketInfoImpl::~SslExtendedSocketInfoImpl() {
  if (cert_validate_result_callback_.has_value()) {
    cert_validate_result_callback_->onSslHandshakeCancelled();
  }
}

void SslExtendedSocketInfoImpl::setCertificateValidationStatus(
    Envoy::Ssl::ClientValidationStatus validated) {
  certificate_validation_status_ = validated;
}

Envoy::Ssl::ClientValidationStatus SslExtendedSocketInfoImpl::certificateValidationStatus() const {
  return certificate_validation_status_;
}

void SslExtendedSocketInfoImpl::onCertificateValidationCompleted(bool succeeded, bool async) {
  cert_validation_result_ =
      succeeded ? Ssl::ValidateStatus::Successful : Ssl::ValidateStatus::Failed;
  if (cert_validate_result_callback_.has_value()) {
    cert_validate_result_callback_.reset();
    // Resume handshake.
    if (async) {
      ssl_handshaker_.handshakeCallbacks()->onAsynchronousCertValidationComplete();
    }
  }
}

Ssl::ValidateResultCallbackPtr SslExtendedSocketInfoImpl::createValidateResultCallback() {
  auto callback = std::make_unique<ValidateResultCallbackImpl>(
      ssl_handshaker_.handshakeCallbacks()->connection().dispatcher(), *this);
  cert_validate_result_callback_ = *callback;
  cert_validation_result_ = Ssl::ValidateStatus::Pending;
  return callback;
}

SslHandshakerImpl::SslHandshakerImpl(bssl::UniquePtr<SSL> ssl, int ssl_extended_socket_info_index,
                                     Ssl::HandshakeCallbacks* handshake_callbacks)
    : ssl_(std::move(ssl)), handshake_callbacks_(handshake_callbacks),
      extended_socket_info_(*this) {
  SSL_set_ex_data(ssl_.get(), ssl_extended_socket_info_index, &(this->extended_socket_info_));
}

bool SslHandshakerImpl::peerCertificateValidated() const {
  return extended_socket_info_.certificateValidationStatus() ==
         Envoy::Ssl::ClientValidationStatus::Validated;
}

Network::PostIoAction SslHandshakerImpl::doHandshake() {
  ASSERT(state_ != Ssl::SocketState::HandshakeComplete && state_ != Ssl::SocketState::ShutdownSent);
  int rc = SSL_do_handshake(ssl());
  if (rc == 1) {
    state_ = Ssl::SocketState::HandshakeComplete;
    handshake_callbacks_->onSuccess(ssl());

    // It's possible that we closed during the handshake callback.
    return handshake_callbacks_->connection().state() == Network::Connection::State::Open
               ? PostIoAction::KeepOpen
               : PostIoAction::Close;
  } else {
    int err = SSL_get_error(ssl(), rc);
    ENVOY_CONN_LOG(trace, "ssl error occurred while read: {}", handshake_callbacks_->connection(),
                   Utility::getErrorDescription(err));
    switch (err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      return PostIoAction::KeepOpen;
    case SSL_ERROR_WANT_PRIVATE_KEY_OPERATION:
    case SSL_ERROR_WANT_CERTIFICATE_VERIFY:
      state_ = Ssl::SocketState::HandshakeInProgress;
      return PostIoAction::KeepOpen;
    default:
      handshake_callbacks_->onFailure();
      return PostIoAction::Close;
    }
  }
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
