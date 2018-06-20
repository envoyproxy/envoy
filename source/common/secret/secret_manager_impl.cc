#include "common/secret/secret_manager_impl.h"

#include "envoy/common/exception.h"

#include "common/secret/secret_manager_util.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

void SecretManagerImpl::addOrUpdateSecret(const std::string& config_source_hash,
                                          const envoy::api::v2::auth::Secret& secret) {
  switch (secret.type_case()) {
  case envoy::api::v2::auth::Secret::TypeCase::kTlsCertificate: {
    std::unique_lock<std::shared_timed_mutex> lhs(tls_certificate_secrets_mutex_);
    tls_certificate_secrets_[config_source_hash][secret.name()] =
        std::make_unique<Ssl::TlsCertificateConfigImpl>(secret.tls_certificate());
  } break;
  default:
    throw EnvoyException("Secret type not implemented");
  }
}

const Ssl::TlsCertificateConfig*
SecretManagerImpl::findTlsCertificate(const std::string& config_source_hash,
                                      const std::string& name) const {
  std::shared_lock<std::shared_timed_mutex> lhs(tls_certificate_secrets_mutex_);

  auto config_source_it = tls_certificate_secrets_.find(config_source_hash);
  if (config_source_it == tls_certificate_secrets_.end()) {
    return nullptr;
  }

  auto secret = config_source_it->second.find(name);
  return (secret != config_source_it->second.end()) ? secret->second.get() : nullptr;
}

std::string SecretManagerImpl::addOrUpdateSdsService(
    const envoy::api::v2::core::ConfigSource& sds_config_source, std::string config_name) {
  std::unique_lock<std::shared_timed_mutex> lhs(sds_api_mutex_);

  auto hash = SecretManagerUtil::configSourceHash(sds_config_source);
  std::string sds_apis_key = hash + config_name;
  if (sds_apis_.find(sds_apis_key) != sds_apis_.end()) {
    return hash;
  }

  sds_apis_[sds_apis_key] =
      std::move(std::make_unique<SdsApi>(server_, sds_config_source, hash, config_name));

  return hash;
}

} // namespace Secret
} // namespace Envoy
