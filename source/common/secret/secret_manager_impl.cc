#include "common/secret/secret_manager_impl.h"

#include "envoy/common/exception.h"

#include "common/secret/secret_manager_util.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

void SecretManagerImpl::addStaticSecret(const envoy::api::v2::auth::Secret& secret) {
  switch (secret.type_case()) {
  case envoy::api::v2::auth::Secret::TypeCase::kTlsCertificate: {
    auto tls_certificate_secret =
        std::make_shared<Ssl::TlsCertificateConfigImpl>(secret.tls_certificate());
    std::unique_lock<std::shared_timed_mutex> lhs(static_tls_certificate_secrets_mutex_);
    static_tls_certificate_secrets_[secret.name()] = tls_certificate_secret;
  } break;
  default:
    throw EnvoyException("Secret type not implemented");
  }
}

const Ssl::TlsCertificateConfigSharedPtr
SecretManagerImpl::findStaticTlsCertificate(const std::string& name) const {
  std::shared_lock<std::shared_timed_mutex> lhs(static_tls_certificate_secrets_mutex_);
  auto secret = static_tls_certificate_secrets_.find(name);
  return (secret != static_tls_certificate_secrets_.end()) ? secret->second : nullptr;
}

DynamicSecretProviderSharedPtr SecretManagerImpl::createDynamicSecretProvider(
    const envoy::api::v2::core::ConfigSource& config_source, std::string config_name) {
  auto hash = SecretManagerUtil::configSourceHash(config_source);
  std::string map_key = hash + config_name;

  std::unique_lock<std::shared_timed_mutex> lhs(dynamic_secret_providers_mutex_);
  auto dynamic_secret_provider = dynamic_secret_providers_[map_key].lock();
  if (!dynamic_secret_provider) {
    dynamic_secret_provider = std::make_shared<SdsApi>(server_, config_source, config_name);
    dynamic_secret_providers_[map_key] = dynamic_secret_provider;
  }

  return dynamic_secret_provider;
}

} // namespace Secret
} // namespace Envoy
