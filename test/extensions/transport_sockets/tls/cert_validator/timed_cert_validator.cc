#include "test/extensions/transport_sockets/tls/cert_validator/timed_cert_validator.h"

#include <openssl/safestack.h>

#include <cstdint>

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

Ssl::ValidateResult TimedCertValidator::doCustomVerifyCertChain(
    STACK_OF(X509) & cert_chain, Ssl::ValidateResultCallbackPtr callback,
    Ssl::SslExtendedSocketInfo* ssl_extended_info,
    const Network::TransportSocketOptions* transport_socket_options, SSL_CTX* ssl_ctx,
    absl::string_view ech_name_override, bool is_server, std::string* /*error_details*/,
    uint8_t* out_alert) {
  std::cerr << "============ doCustomVerifyCertChain \n";
  if (callback == nullptr) {
    ASSERT(ssl_extended_info);
    callback = ssl_extended_info->createValidateResultCallback(out_alert);
  }
  ASSERT(callback_ == nullptr);
  callback_ = std::move(callback);
  ech_name_override_ = std::string(ech_name_override.data(), ech_name_override.size());
  // Store cert chain for the delayed validation.
  for (size_t i = 0; i < sk_X509_num(&cert_chain); i++) {
    X509* cert = sk_X509_value(&cert_chain, i);
    uint8_t* der = nullptr;
    int len = i2d_X509(cert, &der);
    ASSERT(len > 0);
    cert_chain_in_str_.emplace_back(reinterpret_cast<char*>(der), len);
  }
  validation_timer_ = callback_->dispatcher().createTimer([ssl_ctx, transport_socket_options,
                                                           is_server, this]() {
    std::cerr << "=================== on time out " << reinterpret_cast<void*>(ssl_ctx) << "\n";
    bssl::UniquePtr<STACK_OF(X509)> certs(sk_X509_new_null());
    for (auto& cert_str : cert_chain_in_str_) {
      const uint8_t* inp = reinterpret_cast<uint8_t*>(cert_str.data());
      X509* cert = d2i_X509(nullptr, &inp, cert_str.size());
      ASSERT(cert);
      sk_X509_push(certs.get(), cert);
    }
    uint8_t tls_alert{SSL_AD_CERTIFICATE_UNKNOWN};
    std::string error;
    Ssl::ValidateResult result = DefaultCertValidator::doCustomVerifyCertChain(
        *certs, nullptr, nullptr, transport_socket_options, ssl_ctx, ech_name_override_, is_server,
        &error, &tls_alert);
    callback_->onCertValidationResult(result == Ssl::ValidateResult::Successful, error, tls_alert);
  });
  validation_timer_->enableTimer(validation_time_out_ms_);
  return Ssl::ValidateResult::Pending;
}

REGISTER_FACTORY(TimedCertValidatorFactory, CertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
