#pragma once

#include <string>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/secret/dynamic_secret_provider.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {

namespace Server {
namespace Configuration {
class TransportSocketFactoryContext;
} // namespace Configuration
} // namespace Server

namespace Secret {

/**
 * A manager for static and dynamic secrets.
 */
class SecretManager {
public:
  virtual ~SecretManager() {}

  /**
   * @param secret a protobuf message of envoy::api::v2::auth::Secret.
   * @throw an EnvoyException if the secret is invalid or not supported.
   */
  virtual void addStaticSecret(const envoy::api::v2::auth::Secret& secret) PURE;

  /**
   * @param name a name of the Ssl::TlsCertificateConfig.
   * @return the TlsCertificate secret. Returns nullptr if the secret is not found.
   */
  virtual const Ssl::TlsCertificateConfig*
  findStaticTlsCertificate(const std::string& name) const PURE;

  /**
   * Finds and returns a secret provider associated to SDS config. Create a new one
   * if such provider does not exist.
   *
   * @param config_source a protobuf message object contains SDS config source.
   * @param config_name a name that uniquely refers to the SDS config source
   * @param secret_provider_context context that provides components for creating and initializing
   * secret provider.
   * @return the dynamic TLS secret provider.
   */
  virtual DynamicTlsCertificateSecretProviderSharedPtr findOrCreateDynamicSecretProvider(
      const envoy::api::v2::core::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context) PURE;
};

} // namespace Secret
} // namespace Envoy
