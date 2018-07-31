#pragma once

#include <shared_mutex>
#include <unordered_map>

#include "envoy/secret/secret_manager.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/tls_certificate_config.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/secret/sds_api.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager, Logger::Loggable<Logger::Id::upstream> {
public:
  SecretManagerImpl(Server::Instance& server) : server_(server) {}

  void addStaticSecret(const envoy::api::v2::auth::Secret& secret) override;
  const Ssl::TlsCertificateConfig* findStaticTlsCertificate(const std::string& name) const override;

  DynamicTlsCertificateSecretProviderSharedPtr findDynamicTlsCertificateSecretProvider(
      const envoy::api::v2::core::ConfigSource& sds_config_source,
      const std::string& config_name) override;

  void setDynamicTlsCertificateSecretProvider(
      const envoy::api::v2::core::ConfigSource& sds_config_source, const std::string& config_name,
      DynamicTlsCertificateSecretProviderSharedPtr provider) override;

private:
  void removeDeletedSecretProvider();

  Server::Instance& server_;

  // Manages pairs of secret name and Ssl::TlsCertificateConfig.
  std::unordered_map<std::string, Ssl::TlsCertificateConfigPtr> static_tls_certificate_secrets_;

  // map hash code of SDS config source and SdsApi object.
  std::unordered_map<std::string, std::weak_ptr<DynamicTlsCertificateSecretProvider>>
      dynamic_secret_providers_;
};

} // namespace Secret
} // namespace Envoy
