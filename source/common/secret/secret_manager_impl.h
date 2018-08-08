#pragma once

#include <unordered_map>

#include "envoy/secret/secret_manager.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager, Logger::Loggable<Logger::Id::secret> {
public:
  void addStaticSecret(const envoy::api::v2::auth::Secret& secret) override;
  const Ssl::TlsCertificateConfig* findStaticTlsCertificate(const std::string& name) const override;

  DynamicTlsCertificateSecretProviderSharedPtr findOrCreateDynamicSecretProvider(
      const envoy::api::v2::core::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context) override;

private:
  // Manages pairs of secret name and Ssl::TlsCertificateConfig.
  std::unordered_map<std::string, Ssl::TlsCertificateConfigPtr> static_tls_certificate_secrets_;

  // map hash code of SDS config source and SdsApi object.
  std::unordered_map<std::string, std::weak_ptr<DynamicTlsCertificateSecretProvider>>
      dynamic_secret_providers_;
};

} // namespace Secret
} // namespace Envoy
