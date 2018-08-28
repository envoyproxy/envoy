#pragma once

#include <unordered_map>

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager, Logger::Loggable<Logger::Id::secret> {
public:
  void addStaticSecret(const envoy::api::v2::auth::Secret& secret) override;

  TlsCertificateConfigProviderSharedPtr
  findStaticTlsCertificateProvider(const std::string& name) const override;

  TlsCertificateConfigProviderSharedPtr createInlineTlsCertificateProvider(
      const envoy::api::v2::auth::TlsCertificate& tls_certificate) override;

  TlsCertificateConfigProviderSharedPtr findOrCreateTlsCertificateProvider(
      const envoy::api::v2::core::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context) override;

private:
  // Remove dynamic secret provider which has been deleted.
  void removeDynamicSecretProvider(const std::string& map_key);

  // Manages pairs of secret name and TlsCertificateConfigProviderSharedPtr.
  std::unordered_map<std::string, TlsCertificateConfigProviderSharedPtr>
      static_tls_certificate_providers_;

  // map hash code of SDS config source and SdsApi object.
  std::unordered_map<std::string, std::weak_ptr<TlsCertificateConfigProvider>>
      dynamic_secret_providers_;
};

} // namespace Secret
} // namespace Envoy
