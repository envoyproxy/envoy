#include <memory>

#include "envoy/ssl/context_config.h"
#include "envoy/ssl/ssl_socket_extended_info.h"

#include "common/common/macros.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class TestSslExtendedSocketInfo : public Envoy::Ssl::SslExtendedSocketInfo {
public:
  TestSslExtendedSocketInfo(){};

  void setCertificateValidationStatus(Envoy::Ssl::ClientValidationStatus validated) override {
    status_ = validated;
  }
  Envoy::Ssl::ClientValidationStatus certificateValidationStatus() const override {
    return status_;
  }

private:
  Envoy::Ssl::ClientValidationStatus status_;
};

class TestCertificateValidationContextConfig
    : public Envoy::Ssl::CertificateValidationContextConfig {
public:
  TestCertificateValidationContextConfig(envoy::config::core::v3::TypedExtensionConfig config)
      : api_(Api::createApiForTest()), custom_validator_config_(config){};
  TestCertificateValidationContextConfig()
      : api_(Api::createApiForTest()), custom_validator_config_(absl::nullopt){};

  const std::string& caCert() const override { CONSTRUCT_ON_FIRST_USE(std::string, ""); }
  const std::string& caCertPath() const override { CONSTRUCT_ON_FIRST_USE(std::string, ""); }
  const std::string& certificateRevocationList() const override {
    CONSTRUCT_ON_FIRST_USE(std::string, "");
  }
  const std::string& certificateRevocationListPath() const final {
    CONSTRUCT_ON_FIRST_USE(std::string, "");
  }
  const std::vector<std::string>& verifySubjectAltNameList() const override {
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>, {});
  }
  const std::vector<envoy::type::matcher::v3::StringMatcher>&
  subjectAltNameMatchers() const override {
    CONSTRUCT_ON_FIRST_USE(std::vector<envoy::type::matcher::v3::StringMatcher>, {});
  }
  const std::vector<std::string>& verifyCertificateHashList() const override {
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>, {});
  }
  const std::vector<std::string>& verifyCertificateSpkiList() const override {
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>, {});
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
