#include <memory>

#include "envoy/ssl/context_config.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class TestCertificateValidationContextConfig
    : public Envoy::Ssl::CertificateValidationContextConfig {
public:
  TestCertificateValidationContextConfig(envoy::config::core::v3::TypedExtensionConfig config)
      : api_(Api::createApiForTest()), custom_validator_config_(config){};
  TestCertificateValidationContextConfig()
      : api_(Api::createApiForTest()), custom_validator_config_(absl::nullopt){};

  const std::string& caCert() const override {
    static std::string ret = "";
    return ret;
  }
  const std::string& caCertPath() const override {
    static std::string ret = "";
    return ret;
  }
  const std::string& certificateRevocationList() const override {
    static std::string ret = "";
    return ret;
  }
  const std::string& certificateRevocationListPath() const final {
    static std::string ret = "";
    return ret;
  }
  const std::vector<std::string>& verifySubjectAltNameList() const override {
    static std::vector<std::string> ret = {};
    return ret;
  }
  const std::vector<envoy::type::matcher::v3::StringMatcher>&
  subjectAltNameMatchers() const override {
    static std::vector<envoy::type::matcher::v3::StringMatcher> ret = {};
    return ret;
  }
  const std::vector<std::string>& verifyCertificateHashList() const override {
    static std::vector<std::string> ret = {};
    return ret;
  }
  const std::vector<std::string>& verifyCertificateSpkiList() const override {
    static std::vector<std::string> ret = {};
    return ret;
  }
  bool allowExpiredCertificate() const override { return false; }
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
      TrustChainVerification
      trustChainVerification() const override {
    return envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
        TrustChainVerification::
            CertificateValidationContext_TrustChainVerification_ACCEPT_UNTRUSTED;
  }

  const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&
  customValidatorConfig() const override {
    return custom_validator_config_;
  }

  Api::Api& api() override { return *api_; }

private:
  Api::ApiPtr api_;
  const absl::optional<envoy::config::core::v3::TypedExtensionConfig> custom_validator_config_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy