#include <memory>

#include "envoy/ssl/context_config.h"
#include "envoy/ssl/ssl_socket_extended_info.h"

#include "source/common/common/macros.h"
#include "source/common/common/matchers.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class TestSslExtendedSocketInfo : public Envoy::Ssl::SslExtendedSocketInfo {
public:
  TestSslExtendedSocketInfo() = default;

  void setCertificateValidationStatus(Envoy::Ssl::ClientValidationStatus validated) override {
    status_ = validated;
  }
  Envoy::Ssl::ClientValidationStatus certificateValidationStatus() const override {
    return status_;
  }

  Ssl::ValidateResultCallbackPtr createValidateResultCallback() override { return nullptr; };

  void onCertificateValidationCompleted(bool succeeded) override {
    validate_result_ = succeeded ? Ssl::ValidateStatus::Successful : Ssl::ValidateStatus::Failed;
  }
  Ssl::ValidateStatus certificateValidationResult() const override { return validate_result_; }
  uint8_t certificateValidationAlert() const override { return SSL_AD_CERTIFICATE_UNKNOWN; }

private:
  Envoy::Ssl::ClientValidationStatus status_;
  Ssl::ValidateStatus validate_result_{Ssl::ValidateStatus::NotStarted};
};

class TestCertificateValidationContextConfig
    : public Envoy::Ssl::CertificateValidationContextConfig {
public:
  TestCertificateValidationContextConfig(
      envoy::config::core::v3::TypedExtensionConfig custom_config,
      bool allow_expired_certificate = false,
      std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>
          san_matchers = {},
      std::string ca_cert = "", absl::optional<uint32_t> verify_depth = absl::nullopt)
      : allow_expired_certificate_(allow_expired_certificate), api_(Api::createApiForTest()),
        custom_validator_config_(custom_config), san_matchers_(san_matchers), ca_cert_(ca_cert),
        max_verify_depth_(verify_depth){};
  TestCertificateValidationContextConfig()
      : api_(Api::createApiForTest()), custom_validator_config_(absl::nullopt){};

  const std::string& caCert() const override { return ca_cert_; }
  const std::string& caCertPath() const override { return ca_cert_path_; }
  const std::string& certificateRevocationList() const override {
    CONSTRUCT_ON_FIRST_USE(std::string, "");
  }
  const std::string& certificateRevocationListPath() const final {
    CONSTRUCT_ON_FIRST_USE(std::string, "");
  }
  const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>&
  subjectAltNameMatchers() const override {
    return san_matchers_;
  }
  const std::vector<std::string>& verifyCertificateHashList() const override {
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>, {});
  }
  const std::vector<std::string>& verifyCertificateSpkiList() const override {
    CONSTRUCT_ON_FIRST_USE(std::vector<std::string>, {});
  }
  bool allowExpiredCertificate() const override { return allow_expired_certificate_; }
  envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
      TrustChainVerification
      trustChainVerification() const override {
    return envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext::
        TrustChainVerification::
            CertificateValidationContext_TrustChainVerification_VERIFY_TRUST_CHAIN;
  }

  const absl::optional<envoy::config::core::v3::TypedExtensionConfig>&
  customValidatorConfig() const override {
    return custom_validator_config_;
  }

  Api::Api& api() const override { return *api_; }
  bool onlyVerifyLeafCertificateCrl() const override { return false; }

  absl::optional<uint32_t> maxVerifyDepth() const override { return max_verify_depth_; }

private:
  bool allow_expired_certificate_{false};
  Api::ApiPtr api_;
  const absl::optional<envoy::config::core::v3::TypedExtensionConfig> custom_validator_config_;
  const std::vector<envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher>
      san_matchers_{};
  const std::string ca_cert_;
  const std::string ca_cert_path_{"TEST_CA_CERT_PATH"};
  const absl::optional<uint32_t> max_verify_depth_{absl::nullopt};
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
