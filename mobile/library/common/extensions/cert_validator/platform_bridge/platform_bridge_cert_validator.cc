#include "library/common/extensions/cert_validator/platform_bridge/platform_bridge_cert_validator.h"

#include <list>
#include <memory>
#include <type_traits>

#include "library/common/data/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

PlatformBridgeCertValidator::PlatformBridgeCertValidator(
    const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
    const envoy_cert_validator* platform_validator)
    : config_(config), stats_(stats), platform_validator_(platform_validator) {
  ENVOY_BUG(config != nullptr && config->caCert().empty() &&
                config->certificateRevocationList().empty(),
            "Invalid certificate validation context config.");
  if (config_ != nullptr) {
    allow_untrusted_certificate_ = config_->trustChainVerification() ==
                                   envoy::extensions::transport_sockets::tls::v3::
                                       CertificateValidationContext::ACCEPT_UNTRUSTED;
  }
}

PlatformBridgeCertValidator::~PlatformBridgeCertValidator() {
  // Wait for validation threads to finish.
  for (auto& [id, thread] : validation_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

int PlatformBridgeCertValidator::initializeSslContexts(std::vector<SSL_CTX*> /*contexts*/,
                                                       bool /*handshaker_provides_certificates*/) {
  return SSL_VERIFY_PEER;
}

ValidationResults PlatformBridgeCertValidator::doVerifyCertChain(
    STACK_OF(X509) & cert_chain, Ssl::ValidateResultCallbackPtr callback,
    Ssl::SslExtendedSocketInfo* ssl_extended_info,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
    SSL_CTX& /*ssl_ctx*/, const CertValidator::ExtraValidationContext& /*validation_context*/,
    bool is_server, absl::string_view host_name) {
  ASSERT(!is_server);
  if (sk_X509_num(&cert_chain) == 0) {
    if (ssl_extended_info) {
      ssl_extended_info->setCertificateValidationStatus(
          Envoy::Ssl::ClientValidationStatus::NotValidated);
    }
    const char* error = "verify cert chain failed: empty cert chain.";
    stats_.fail_verify_error_.inc();
    ENVOY_LOG(debug, error);
    return {ValidationResults::ValidationStatus::Failed, absl::nullopt, error};
  }
  if (callback == nullptr) {
    callback = ssl_extended_info->createValidateResultCallback();
  }

  std::vector<envoy_data> certs;
  for (uint64_t i = 0; i < sk_X509_num(&cert_chain); i++) {
    X509* cert = sk_X509_value(&cert_chain, i);
    unsigned char* cert_in_der = nullptr;
    int der_length = i2d_X509(cert, &cert_in_der);
    ASSERT(der_length > 0 && cert_in_der != nullptr);

    absl::string_view cert_str(reinterpret_cast<char*>(cert_in_der),
                               static_cast<size_t>(der_length));
    certs.push_back(Data::Utility::copyToBridgeData(cert_str));
    OPENSSL_free(cert_in_der);
  }

  absl::string_view host;
  if (transport_socket_options != nullptr &&
      !transport_socket_options->verifySubjectAltNameListOverride().empty()) {
    host = transport_socket_options->verifySubjectAltNameListOverride()[0];
  } else {
    host = host_name;
  }
  auto validation = std::make_unique<PendingValidation>(
      *this, std::move(certs), host, std::move(transport_socket_options), std::move(callback));
  PendingValidation* validation_ptr = validation.get();
  validations_.insert(std::move(validation));
  std::thread verification_thread(&PendingValidation::verifyCertsByPlatform, validation_ptr);
  std::thread::id thread_id = verification_thread.get_id();
  validation_threads_[thread_id] = std::move(verification_thread);
  return {ValidationResults::ValidationStatus::Pending, absl::nullopt, absl::nullopt};
}

void PlatformBridgeCertValidator::verifyCertChainByPlatform(
    std::vector<envoy_data>& cert_chain, const std::string& host_name,
    const std::vector<std::string>& subject_alt_names, PendingValidation& pending_validation) {
  ASSERT(!cert_chain.empty());
  ENVOY_LOG(trace, "Start verifyCertChainByPlatform for host {}", host_name);
  // This is running in a stand alone thread other than the engine thread.
  envoy_data leaf_cert_der = cert_chain[0];
  bssl::UniquePtr<X509> leaf_cert(d2i_X509(
      nullptr, const_cast<const unsigned char**>(&leaf_cert_der.bytes), leaf_cert_der.length));
  envoy_cert_validation_result result =
      platform_validator_->validate_cert(cert_chain.data(), cert_chain.size(), host_name.c_str());
  bool success = result.result == ENVOY_SUCCESS;
  if (!success) {
    ENVOY_LOG(debug, result.error_details);
    pending_validation.postVerifyResultAndCleanUp(/*success=*/allow_untrusted_certificate_,
                                                  result.error_details, result.tls_alert,
                                                  makeOptRef(stats_.fail_verify_error_));
    return;
  }

  absl::string_view error_details;
  // Verify that host name matches leaf cert.
  success = DefaultCertValidator::verifySubjectAltName(leaf_cert.get(), subject_alt_names);
  if (!success) {
    error_details = "PlatformBridgeCertValidator_verifySubjectAltName failed: SNI mismatch.";
    ENVOY_LOG(debug, error_details);
    pending_validation.postVerifyResultAndCleanUp(/*success=*/allow_untrusted_certificate_,
                                                  error_details, SSL_AD_BAD_CERTIFICATE,
                                                  makeOptRef(stats_.fail_verify_san_));
    return;
  }
  pending_validation.postVerifyResultAndCleanUp(success, error_details, SSL_AD_CERTIFICATE_UNKNOWN,
                                                {});
}

void PlatformBridgeCertValidator::PendingValidation::verifyCertsByPlatform() {
  parent_.verifyCertChainByPlatform(
      certs_, host_name_,
      (transport_socket_options_ != nullptr
           ? transport_socket_options_->verifySubjectAltNameListOverride()
           : std::vector<std::string>{host_name_}),
      *this);
}

void PlatformBridgeCertValidator::PendingValidation::postVerifyResultAndCleanUp(
    bool success, absl::string_view error_details, uint8_t tls_alert,
    OptRef<Stats::Counter> error_counter) {
  ENVOY_LOG(trace,
            "Finished platform cert validation for {}, post result callback to network thread",
            host_name_);

  if (parent_.platform_validator_->release_validator) {
    parent_.platform_validator_->release_validator();
  }
  std::weak_ptr<size_t> weak_alive_indicator(parent_.alive_indicator_);

  // Once this task runs, `this` will be deleted so this must be the last statement in the file.
  result_callback_->dispatcher().post([this, weak_alive_indicator, success,
                                       error = std::string(error_details), tls_alert, error_counter,
                                       thread_id = std::this_thread::get_id()]() {
    if (weak_alive_indicator.expired()) {
      return;
    }
    ENVOY_LOG(trace, "Got validation result for {} from platform", host_name_);
    parent_.validation_threads_[thread_id].join();
    parent_.validation_threads_.erase(thread_id);
    if (error_counter.has_value()) {
      const_cast<Stats::Counter&>(error_counter.ref()).inc();
    }
    result_callback_->onCertValidationResult(success, error, tls_alert);
    parent_.validations_.erase(this);
  });
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
