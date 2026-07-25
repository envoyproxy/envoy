#include "source/common/tls/ssl_handshaker.h"

#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/tls/context_impl.h"
#include "source/common/tls/utility.h"

using Envoy::Network::PostIoAction;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

namespace {

// Applies per-certificate TLS parameters directly to a SSL* after SSL_set_SSL_CTX, which only
// transfers certificate material and does not propagate cipher/version/curve settings.
absl::Status applyTlsParamsToSsl(const Ssl::TlsParams& p, const Ssl::TlsContext& ctx, SSL* ssl) {
  using TlsProto = envoy::extensions::transport_sockets::tls::v3::TlsParameters;
  if (p.min_protocol_version != TlsProto::TLS_AUTO) {
    if (SSL_set_min_proto_version(ssl, Utility::tlsVersionFromProto(p.min_protocol_version, 0)) !=
        1) {
      return absl::InternalError(absl::StrCat("Failed to set per-cert min TLS version: ",
                                              Utility::getLastCryptoError().value_or("")));
    }
  }
  if (p.max_protocol_version != TlsProto::TLS_AUTO) {
    if (SSL_set_max_proto_version(ssl, Utility::tlsVersionFromProto(p.max_protocol_version, 0)) !=
        1) {
      return absl::InternalError(absl::StrCat("Failed to set per-cert max TLS version: ",
                                              Utility::getLastCryptoError().value_or("")));
    }
  }
  // Skip cipher/curve/sigalg fields when a custom handshaker manages them; applying them here
  // would clobber handshaker-provided configuration.
  if (!ctx.provides_ciphers_and_curves_) {
    if (!p.cipher_suites.empty() && SSL_set_strict_cipher_list(ssl, p.cipher_suites.c_str()) != 1) {
      return absl::InternalError(absl::StrCat("Failed to set per-cert cipher suites: ",
                                              Utility::getLastCryptoError().value_or("")));
    }
    if (!p.ecdh_curves.empty() && SSL_set1_curves_list(ssl, p.ecdh_curves.c_str()) != 1) {
      return absl::InternalError(absl::StrCat("Failed to set per-cert ECDH curves: ",
                                              Utility::getLastCryptoError().value_or("")));
    }
  }
  if (!ctx.provides_sigalgs_ && !p.signature_algorithms.empty() &&
      SSL_set1_sigalgs_list(ssl, p.signature_algorithms.c_str()) != 1) {
    return absl::InternalError(absl::StrCat("Failed to set per-cert signature algorithms: ",
                                            Utility::getLastCryptoError().value_or("")));
  }
  // Compliance policy must be applied last.
  if (p.compliance_policy.has_value()) {
    auto ssl_policy_or_error = Utility::compliancePolicyToSslPolicy(p.compliance_policy.value());
    if (!ssl_policy_or_error.ok()) {
      return ssl_policy_or_error.status();
    }
    if (SSL_set_compliance_policy(ssl, *ssl_policy_or_error) != 1) {
      return absl::InternalError(absl::StrCat("Failed to set per-cert compliance policy: ",
                                              Utility::getLastCryptoError().value_or("")));
    }
  }
  return absl::OkStatus();
}

} // namespace

void ValidateResultCallbackImpl::onSslHandshakeCancelled() { extended_socket_info_.reset(); }

void ValidateResultCallbackImpl::onCertValidationResult(bool succeeded,
                                                        Ssl::ClientValidationStatus detailed_status,
                                                        const std::string& error_details,
                                                        uint8_t tls_alert) {
  if (!extended_socket_info_.has_value()) {
    return;
  }
  extended_socket_info_->setCertificateValidationStatus(detailed_status);
  extended_socket_info_->setCertificateValidationAlert(tls_alert);
  if (!error_details.empty()) {
    extended_socket_info_->setCertificateValidationError(error_details);
  }
  extended_socket_info_->onCertificateValidationCompleted(succeeded, true);
}

void CertificateSelectionCallbackImpl::onSslHandshakeCancelled() { extended_socket_info_.reset(); }

void CertificateSelectionCallbackImpl::onCertificateSelectionResult(
    OptRef<const Ssl::TlsContext> selected_ctx, bool staple) {
  if (!extended_socket_info_.has_value()) {
    return;
  }
  extended_socket_info_->onCertificateSelectionCompleted(selected_ctx, staple, true);
}

SslExtendedSocketInfoImpl::~SslExtendedSocketInfoImpl() {
  if (cert_validate_result_callback_.has_value()) {
    cert_validate_result_callback_->onSslHandshakeCancelled();
  }
  if (cert_selection_callback_.has_value()) {
    cert_selection_callback_->onSslHandshakeCancelled();
  }
}

void SslExtendedSocketInfoImpl::setCertificateValidationStatus(
    Envoy::Ssl::ClientValidationStatus validated) {
  certificate_validation_status_ = validated;
}

void SslExtendedSocketInfoImpl::setValidatedCertChain(std::vector<bssl::UniquePtr<X509>> chain) {
  ssl_handshaker_.setValidatedCertChain(std::move(chain));
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

void SslExtendedSocketInfoImpl::onCertificateSelectionCompleted(
    OptRef<const Ssl::TlsContext> selected_ctx, bool staple, bool async) {
  RELEASE_ASSERT(cert_selection_result_ == Ssl::CertificateSelectionStatus::Pending,
                 "onCertificateSelectionCompleted twice");
  if (!selected_ctx.has_value()) {
    cert_selection_result_ = Ssl::CertificateSelectionStatus::Failed;
  } else {
    cert_selection_result_ = Ssl::CertificateSelectionStatus::Successful;
    // Apply the selected context. This must be done before OCSP stapling below
    // since applying the context can remove the previously-set OCSP response.
    // This will only return NULL if memory allocation fails.
    RELEASE_ASSERT(SSL_set_SSL_CTX(ssl_handshaker_.ssl(), selected_ctx->ssl_ctx_.get()) != nullptr,
                   "");
    // tls_params_ is only reached here (the TLS cert_cb path). QUIC/HTTP3 downstream selects
    // certificates via quic::ProofSource::GetProof() and never calls this function, so
    // per-certificate tls_params have no effect on QUIC connections.
    bool params_ok = true;
    if (selected_ctx->tls_params_.has_value()) {
      if (absl::Status s =
              applyTlsParamsToSsl(*selected_ctx->tls_params_, *selected_ctx, ssl_handshaker_.ssl());
          !s.ok()) {
        ENVOY_LOG_MISC(warn, "Failed to apply per-certificate tls_params: {}", s.message());
        cert_selection_result_ = Ssl::CertificateSelectionStatus::Failed;
        params_ok = false;
      }
    }

    if (params_ok && staple) {
      // We avoid setting the OCSP response if the client didn't request it, but doing so is safe.
      RELEASE_ASSERT(selected_ctx->ocsp_response_,
                     "OCSP response must be present under OcspStapleAction::Staple");
      const std::vector<uint8_t>& resp_bytes = selected_ctx->ocsp_response_->rawBytes();
      const int rc =
          SSL_set_ocsp_response(ssl_handshaker_.ssl(), resp_bytes.data(), resp_bytes.size());
      RELEASE_ASSERT(rc != 0, "");
    }
  }
  if (cert_selection_callback_.has_value()) {
    cert_selection_callback_.reset();
    // Resume handshake.
    if (async) {
      ssl_handshaker_.handshakeCallbacks()->onAsynchronousCertificateSelectionComplete();
    }
  }
}

Ssl::CertificateSelectionCallbackPtr
SslExtendedSocketInfoImpl::createCertificateSelectionCallback() {
  auto callback = std::make_unique<CertificateSelectionCallbackImpl>(
      ssl_handshaker_.handshakeCallbacks()->connection().dispatcher(), *this);
  cert_selection_callback_ = *callback;
  cert_selection_result_ = Ssl::CertificateSelectionStatus::Pending;
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
      state_ = Ssl::SocketState::HandshakeWaitingForConnectionData;
      return PostIoAction::KeepOpen;
    case SSL_ERROR_PENDING_CERTIFICATE:
    case SSL_ERROR_WANT_PRIVATE_KEY_OPERATION:
    case SSL_ERROR_WANT_CERTIFICATE_VERIFY:
    case SSL_ERROR_WANT_X509_LOOKUP:
      state_ = Ssl::SocketState::HandshakeBlockedOnAsyncOperation;
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
