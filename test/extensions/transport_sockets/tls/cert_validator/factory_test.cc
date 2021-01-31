#include "extensions/transport_sockets/tls/cert_validator/factory.h"

#include <memory>

#include "envoy/ssl/context_config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class TestCertificateValidationContextConfig
    : public Envoy::Ssl::CertificateValidationContextConfig {
public:
  TestCertificateValidationContextConfig(envoy::config::core::v3::TypedExtensionConfig config)
      : custom_validator_config_(config){};
  TestCertificateValidationContextConfig() : custom_validator_config_(absl::nullopt){};

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

private:
  const absl::optional<envoy::config::core::v3::TypedExtensionConfig> custom_validator_config_;
};

TEST(FactoryTest, TestGetCertValidatorName) {
  EXPECT_EQ("envoy.tls.cert_validator.default", getCertValidatorName(nullptr));
  auto config = std::make_unique<TestCertificateValidationContextConfig>();
  EXPECT_EQ("envoy.tls.cert_validator.default", getCertValidatorName(config.get()));

  envoy::config::core::v3::TypedExtensionConfig custom_config = {};
  custom_config.set_name("envoy.tls.cert_validator.spiffe");
  config = std::make_unique<TestCertificateValidationContextConfig>(custom_config);
  EXPECT_EQ(custom_config.name(), getCertValidatorName(config.get()));
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
