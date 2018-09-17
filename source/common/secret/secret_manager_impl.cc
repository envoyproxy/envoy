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

void SecretManagerImpl::removeDynamicSecretProvider(const std::string& map_key) {
  ENVOY_LOG(debug, "Unregister secret provider. hash key: {}", map_key);

  auto num_deleted = dynamic_secret_providers_.erase(map_key);
  ASSERT(num_deleted == 1, "");
}

SdsApiSharedPtr SecretManagerImpl::findOrCreate(
    const envoy::api::v2::core::ConfigSource& sds_config_source, const std::string& config_name,
    std::function<SdsApiSharedPtr(std::function<void()> unregister_secret_provider)> create_fn) {
  const std::string map_key = sds_config_source.SerializeAsString() + config_name;

  SdsApiSharedPtr secret_provider = dynamic_secret_providers_[map_key].lock();
  if (!secret_provider) {
    // SdsApi is owned by ListenerImpl and ClusterInfo which are destroyed before
    // SecretManagerImpl. It is safe to invoke this callback at the destructor of SdsApi.
    std::function<void()> unregister_secret_provider = [map_key, this]() {
      removeDynamicSecretProvider(map_key);
    };

    secret_provider = create_fn(unregister_secret_provider);
    dynamic_secret_providers_[map_key] = secret_provider;
  }
  return secret_provider;
}

TlsCertificateConfigProviderSharedPtr SecretManagerImpl::findOrCreateTlsCertificateProvider(
    const envoy::api::v2::core::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context) {
  auto create_fn = [&secret_provider_context, &sds_config_source, &config_name](
                       std::function<void()> unregister_secret_provider) -> SdsApiSharedPtr {
    ASSERT(secret_provider_context.initManager() != nullptr);
    return TlsCertificateSdsApi::create(secret_provider_context, sds_config_source, config_name,
                                        unregister_secret_provider);
  };
  SdsApiSharedPtr secret_provider = findOrCreate(sds_config_source, config_name, create_fn);

  return std::dynamic_pointer_cast<TlsCertificateConfigProvider>(secret_provider);
}

CertificateValidationContextConfigProviderSharedPtr
SecretManagerImpl::findOrCreateCertificateValidationContextProvider(
    const envoy::api::v2::core::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context) {
  auto create_fn = [&secret_provider_context, &sds_config_source, &config_name](
                       std::function<void()> unregister_secret_provider) -> SdsApiSharedPtr {
    ASSERT(secret_provider_context.initManager() != nullptr);
    return CertificateValidationContextSdsApi::create(secret_provider_context, sds_config_source,
                                                      config_name, unregister_secret_provider);
  };
  SdsApiSharedPtr secret_provider = findOrCreate(sds_config_source, config_name, create_fn);

  return std::dynamic_pointer_cast<CertificateValidationContextConfigProvider>(secret_provider);
}

} // namespace Secret
} // namespace Envoy
