#include "common/secret/secret_manager_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/secret/sds_api.h"
#include "common/secret/secret_provider_impl.h"
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
  default:
    throw EnvoyException("Secret type not implemented");
  }
}

TlsCertificateConfigProviderSharedPtr
SecretManagerImpl::findStaticTlsCertificateProvider(const std::string& name) const {
  auto secret = static_tls_certificate_providers_.find(name);
  return (secret != static_tls_certificate_providers_.end()) ? secret->second : nullptr;
}

TlsCertificateConfigProviderSharedPtr SecretManagerImpl::createInlineTlsCertificateProvider(
    const envoy::api::v2::auth::TlsCertificate& tls_certificate) {
  return std::make_shared<TlsCertificateConfigProviderImpl>(tls_certificate);
}

void SecretManagerImpl::removeDynamicSecretProvider(const std::string& map_key) {
  ENVOY_LOG(debug, "Unregister secret provider. hash key: {}", map_key);

  if (dynamic_secret_providers_.erase(map_key) == 0) {
    ENVOY_LOG(error, "secret provider does not exist. hash key: {}", map_key);
  }
}

TlsCertificateConfigProviderSharedPtr SecretManagerImpl::findOrCreateDynamicSecretProvider(
    const envoy::api::v2::core::ConfigSource& sds_config_source, const std::string& config_name,
    Server::Configuration::TransportSocketFactoryContext& secret_provider_context) {
  std::string map_key = std::to_string(MessageUtil::hash(sds_config_source)) + config_name;

  auto secret_provider = dynamic_secret_providers_[map_key].lock();
  if (!secret_provider) {
    ASSERT(secret_provider_context.initManager() != nullptr);

    std::function<void()> unregister_secret_provider = [map_key, this]() {
      this->removeDynamicSecretProvider(map_key);
    };

    secret_provider = std::make_shared<SdsApi>(
        secret_provider_context.localInfo(), secret_provider_context.dispatcher(),
        secret_provider_context.random(), secret_provider_context.stats(),
        secret_provider_context.clusterManager(), *secret_provider_context.initManager(),
        sds_config_source, config_name, unregister_secret_provider);
    dynamic_secret_providers_[map_key] = secret_provider;
  }

  return secret_provider;
}

} // namespace Secret
} // namespace Envoy
