#pragma once

#include <openssl/ssl.h>

#include <chrono>
#include <memory>

#include "source/common/tls/cert_validator/default_validator.h"
#include "source/common/tls/cert_validator/factory.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// A cert validator which defers the validation by certain time.
class TimedCertValidator : public DefaultCertValidator {
public:
  TimedCertValidator(std::chrono::milliseconds validation_time_out_ms,
                     const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
                     Server::Configuration::CommonFactoryContext& context,
                     absl::optional<std::string> expected_host_name)
      : DefaultCertValidator(config, stats, context),
        validation_time_out_ms_(validation_time_out_ms), expected_host_name_(expected_host_name) {}

  ValidationResults
  doVerifyCertChain(STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl_ctx,
                    const CertValidator::ExtraValidationContext& validation_context, bool is_server,
                    absl::string_view host_name) override;

  bool validationPending() const { return validation_timer_->enabled(); }
  void setExpectedLocalAddress(absl::string_view expected_local_address) {
    expected_local_address_ = expected_local_address;
  }
  void setExpectedPeerAddress(absl::string_view expected_peer_address) {
    expected_peer_address_ = expected_peer_address;
  }

private:
  Event::TimerPtr validation_timer_;
  std::chrono::milliseconds validation_time_out_ms_;
  Ssl::ValidateResultCallbackPtr callback_;
  std::vector<std::string> cert_chain_in_str_;
  absl::optional<std::string> expected_host_name_;
  absl::optional<std::string> expected_local_address_;
  absl::optional<std::string> expected_peer_address_;
};

class TimedCertValidatorFactory : public CertValidatorFactory {
public:
  CertValidatorPtr
  createCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
                      Server::Configuration::CommonFactoryContext& context) override {
    auto validator = std::make_unique<TimedCertValidator>(validation_time_out_ms_, config, stats,
                                                          context, expected_host_name_);
    if (expected_peer_address_.has_value()) {
      validator->setExpectedPeerAddress(expected_peer_address_.value());
    }
    return validator;
  }

  std::string name() const override { return "envoy.tls.cert_validator.timed_cert_validator"; }

  void setValidationTimeOutMs(std::chrono::milliseconds validation_time_out_ms) {
    validation_time_out_ms_ = validation_time_out_ms;
  }

  void setExpectedHostName(const std::string host_name) { expected_host_name_ = host_name; }
  void setExpectedPeerAddress(absl::string_view expected_peer_address) {
    expected_peer_address_ = expected_peer_address;
  }

  void resetForTest() {
    validation_time_out_ms_ = std::chrono::milliseconds(5);
    expected_host_name_.reset();
    expected_peer_address_.reset();
  }

private:
  std::chrono::milliseconds validation_time_out_ms_{5};
  absl::optional<std::string> expected_host_name_;
  absl::optional<std::string> expected_peer_address_;
};

DECLARE_FACTORY(TimedCertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
