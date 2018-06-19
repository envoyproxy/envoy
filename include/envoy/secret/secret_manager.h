#pragma once

#include <string>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {
namespace Secret {

/**
 * A manager for static secrets.
 *
 * TODO(jaebong) Support dynamic secrets.
 */
class SecretManager {
public:
  virtual ~SecretManager() {}

  /**
   * @param config_source_hash a hash string of normalized config source. If it is empty string,
   *                           find secret from the static secrets.
   * @param secret a protobuf message of envoy::api::v2::auth::Secret.
   * @throw an EnvoyException if the secret is invalid or not supported.
   */
  virtual void addOrUpdateSecret(const std::string& config_source_hash,
                                 const envoy::api::v2::auth::Secret& secret) PURE;

  /**
   * @param sds_config_source_hash hash string of normalized config source.
   * @param name a name of the Ssl::TlsCertificateConfig.
   * @return the TlsCertificate secret. Returns nullptr if the secret is not found.
   */
  virtual const Ssl::TlsCertificateConfig* findTlsCertificate(const std::string& config_source_hash,
                                                              const std::string& name) const PURE;

  /**
   * Add or update SDS config source. SecretManager starts downloading secrets from registered
   * config source.
   *
   * @param sdsConfigSource a protobuf message object contains SDS config source.
   * @param config_name a name that uniquely refers to the SDS config source
   * @return a hash string of normalized config source
   */
  virtual std::string addOrUpdateSdsService(const envoy::api::v2::core::ConfigSource& config_source,
                                            std::string config_name) PURE;
};

} // namespace Secret
} // namespace Envoy
