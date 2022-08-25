#include "test/extensions/transport_sockets/tls/cert_validator/timed_cert_validator.h"

#include <openssl/safestack.h>

#include <cstdint>

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

ValidationResults TimedCertValidator::doVerifyCertChain(
    STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback,
    Ssl::SslExtendedSocketInfo* ssl_extended_info,
    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options, SSL_CTX& ssl_ctx,
    const CertValidator::ExtraValidationContext& /*validation_context*/, bool is_server,
    absl::string_view host_name) {
  if (callback == nullptr) {
    ASSERT(ssl_extended_info);
    callback = ssl_extended_info->createValidateResultCallback();
  }
  ASSERT(callback_ == nullptr);
  callback_ = std::move(callback);
  if (expected_host_name_.has_value()) {
    EXPECT_EQ(expected_host_name_.value(), host_name);
  }
  // Store cert chain for the delayed validation.
  for (size_t i = 0; i < sk_X509_num(&cert_chain); i++) {
    X509* cert = sk_X509_value(&cert_chain, i);
    uint8_t* der = nullptr;
    int len = i2d_X509(cert, &der);
    ASSERT(len > 0);
    cert_chain_in_str_.emplace_back(reinterpret_cast<char*>(der), len);
    OPENSSL_free(der);
  }
  validation_timer_ = callback_->dispatcher().createTimer(
      [&ssl_ctx, transport_socket_options, is_server, host = std::string(host_name), this]() {
        bssl::UniquePtr<STACK_OF(X509)> certs(sk_X509_new_null());
        for (auto& cert_str : cert_chain_in_str_) {
          const uint8_t* inp = reinterpret_cast<uint8_t*>(cert_str.data());
          X509* cert = d2i_X509(nullptr, &inp, cert_str.size());
          ASSERT(cert);
          if (!bssl::PushToStack(certs.get(), bssl::UniquePtr<X509>(cert))) {
            PANIC("boring SSL object allocation failed.");
          }
        }
        ValidationResults result = DefaultCertValidator::doVerifyCertChain(
            *certs, nullptr, nullptr, transport_socket_options, ssl_ctx, {}, is_server, host);
        callback_->onCertValidationResult(
            result.status == ValidationResults::ValidationStatus::Successful,
            (result.error_details.has_value() ? result.error_details.value() : ""),
            (result.tls_alert.has_value() ? result.tls_alert.value() : SSL_AD_CERTIFICATE_UNKNOWN));
      });
  validation_timer_->enableTimer(validation_time_out_ms_);
  return {ValidationResults::ValidationStatus::Pending, absl::nullopt, absl::nullopt};
}

REGISTER_FACTORY(TimedCertValidatorFactory, CertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
