#pragma once

#include <unordered_map>

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager, Logger::Loggable<Logger::Id::secret> {
public:
  void addStaticSecret(const envoy::api::v2::auth::Secret& secret) override;

  TlsCertificateConfigProviderSharedPtr
  findStaticTlsCertificateProvider(const std::string& name) const override;

  CertificateValidationContextConfigProviderSharedPtr
  findStaticCertificateValidationContextProvider(const std::string& name) const override;

  TlsCertificateConfigProviderSharedPtr createInlineTlsCertificateProvider(
      const envoy::api::v2::auth::TlsCertificate& tls_certificate) override;

  CertificateValidationContextConfigProviderSharedPtr
  createInlineCertificateValidationContextProvider(
      const envoy::api::v2::auth::CertificateValidationContext& certificate_validation_context)
      override;

private:
  // Manages pairs of secret name and TlsCertificateConfigProviderSharedPtr.
  std::unordered_map<std::string, TlsCertificateConfigProviderSharedPtr>
      static_tls_certificate_providers_;

  // Manages pairs of secret name and CertificateValidationContextConfigProviderSharedPtr.
  std::unordered_map<std::string, CertificateValidationContextConfigProviderSharedPtr>
      static_certificate_validation_context_providers_;
};

} // namespace Secret
} // namespace Envoy
