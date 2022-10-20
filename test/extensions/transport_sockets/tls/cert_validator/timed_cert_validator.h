#include <openssl/ssl.h>

#include <chrono>
#include <memory>

#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/cert_validator/factory.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// A cert validator which defers the validation by certain time.
class TimedCertValidator : public DefaultCertValidator {
public:
  TimedCertValidator(std::chrono::milliseconds validation_time_out_ms,
                     const Envoy::Ssl::CertificateValidationContextConfig* config, SslStats& stats,
                     TimeSource& time_source, absl::optional<std::string> expected_host_name)
      : DefaultCertValidator(config, stats, time_source),
        validation_time_out_ms_(validation_time_out_ms), expected_host_name_(expected_host_name) {}

  int doSynchronousVerifyCertChain(
      X509_STORE_CTX* /*store_ctx*/, Ssl::SslExtendedSocketInfo* /*ssl_extended_info*/,
      X509& /*leaf_cert*/,
      const Network::TransportSocketOptions* /*transport_socket_options*/) override {
    PANIC("unimplemented");
  }

  ValidationResults
  doVerifyCertChain(STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    Ssl::SslExtendedSocketInfo* ssl_extended_info,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl_ctx,
                    const CertValidator::ExtraValidationContext& validation_context, bool is_server,
                    absl::string_view host_name) override;

  bool validationPending() const { return validation_timer_->enabled(); }

private:
  Event::TimerPtr validation_timer_;
  std::chrono::milliseconds validation_time_out_ms_;
  Ssl::ValidateResultCallbackPtr callback_;
  std::vector<std::string> cert_chain_in_str_;
  absl::optional<std::string> expected_host_name_;
};

class TimedCertValidatorFactory : public CertValidatorFactory {
public:
  CertValidatorPtr createCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config,
                                       SslStats& stats, TimeSource& time_source) override {
    return std::make_unique<TimedCertValidator>(validation_time_out_ms_, config, stats, time_source,
                                                expected_host_name_);
  }

  std::string name() const override { return "envoy.tls.cert_validator.timed_cert_validator"; }

  void setValidationTimeOutMs(std::chrono::milliseconds validation_time_out_ms) {
    validation_time_out_ms_ = validation_time_out_ms;
  }

  void setExpectedHostName(const std::string host_name) { expected_host_name_ = host_name; }

  void resetForTest() {
    validation_time_out_ms_ = std::chrono::milliseconds(5);
    expected_host_name_.reset();
  }

private:
  std::chrono::milliseconds validation_time_out_ms_{5};
  absl::optional<std::string> expected_host_name_;
};

DECLARE_FACTORY(TimedCertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
