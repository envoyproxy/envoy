#include "common/secret/secret_manager_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/secret/sds_api.h"
#include "common/secret/secret_provider_impl.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

void SecretManagerImpl::addStaticSecret(const envoy::api::v2::auth::Secret& secret) {
  switch (secret.type_case()) {
  case envoy::api::v2::auth::Secret::TypeCase::kTlsCertificate: {
    auto secret_provider =
        std::make_shared<TlsCertificateConfigProviderImpl>(secret.tls_certificate());
    if (!static_tls_certificate_providers_.insert(std::make_pair(secret.name(), secret_provider))
             .second) {
      throw EnvoyException(
          fmt::format("Duplicate static TlsCertificate secret name {}", secret.name()));
    }
    break;
  }
  case envoy::api::v2::auth::Secret::TypeCase::kValidationContext: {
    auto secret_provider = std::make_shared<CertificateValidationContextConfigProviderImpl>(
        secret.validation_context());
    if (!static_certificate_validation_context_providers_
             .insert(std::make_pair(secret.name(), secret_provider))
             .second) {
      throw EnvoyException(fmt::format(
          "Duplicate static CertificateValidationContext secret name {}", secret.name()));
    }
    break;
  }
  default:
    throw EnvoyException("Secret type not implemented");
  }
}

TlsCertificateConfigProviderSharedPtr
SecretManagerImpl::findStaticTlsCertificateProvider(const std::string& name) const {
  auto secret = static_tls_certificate_providers_.find(name);
  return (secret != static_tls_certificate_providers_.end()) ? secret->second : nullptr;
}

CertificateValidationContextConfigProviderSharedPtr
SecretManagerImpl::findStaticCertificateValidationContextProvider(const std::string& name) const {
  auto secret = static_certificate_validation_context_providers_.find(name);
  return (secret != static_certificate_validation_context_providers_.end()) ? secret->second
                                                                            : nullptr;
}

TlsCertificateConfigProviderSharedPtr SecretManagerImpl::createInlineTlsCertificateProvider(
    const envoy::api::v2::auth::TlsCertificate& tls_certificate) {
  return std::make_shared<TlsCertificateConfigProviderImpl>(tls_certificate);
}

CertificateValidationContextConfigProviderSharedPtr
SecretManagerImpl::createInlineCertificateValidationContextProvider(
    const envoy::api::v2::auth::CertificateValidationContext& certificate_validation_context) {
  return std::make_shared<CertificateValidationContextConfigProviderImpl>(
      certificate_validation_context);
}

TlsCertificateConfigProviderSharedPtr SecretManagerImpl::findOrCreateTlsCertificateProvider(
    const envoy::api::v2::core::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context) {
  return certificate_providers_.findOrCreate(sds_config_source, config_name,
                                             secret_provider_context);
}

CertificateValidationContextConfigProviderSharedPtr
SecretManagerImpl::findOrCreateCertificateValidationContextProvider(
    const envoy::api::v2::core::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context) {
  return validation_context_providers_.findOrCreate(sds_config_source, config_name,
                                                    secret_provider_context);
}

} // namespace Secret
} // namespace Envoy
