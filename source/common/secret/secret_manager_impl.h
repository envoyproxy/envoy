#pragma once

#include <shared_mutex>
#include <unordered_map>

#include "envoy/secret/secret_manager.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "common/common/logger.h"
#include "common/secret/sds_api.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager, Logger::Loggable<Logger::Id::upstream> {
public:
  SecretManagerImpl(Server::Instance& server) : server_(server) {}

  void addStaticSecret(const envoy::api::v2::auth::Secret& secret) override;

  const Ssl::TlsCertificateConfigSharedPtr
  findStaticTlsCertificate(const std::string& name) const override;

  virtual DynamicSecretProviderSharedPtr
  createDynamicSecretProvider(const envoy::api::v2::core::ConfigSource& config_source,
                              std::string config_name) override;

private:
  Server::Instance& server_;

  // Manages pairs of name and Ssl::TlsCertificateConfig grouped by SDS config source hash.
  // If SDS config source hash is empty, it is a static secret.
  std::unordered_map<std::string, Ssl::TlsCertificateConfigSharedPtr>
      static_tls_certificate_secrets_;
  mutable std::shared_timed_mutex static_tls_certificate_secrets_mutex_;

  // map hash code of SDS config source and SdsApi object.
  std::unordered_map<std::string, std::weak_ptr<DynamicSecretProvider>> dynamic_secret_providers_;
  mutable std::shared_timed_mutex dynamic_secret_providers_mutex_;
};

} // namespace Secret
} // namespace Envoy
