#pragma once

#include <string>

#include "envoy/secret/secret_callbacks.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {
namespace Secret {

/**
 * An interface to fetch dynamic secret.
 */
class DynamicSecretProvider {
public:
  virtual ~DynamicSecretProvider() {}

  /**
   * @return the TlsCertificate secret. Returns nullptr if the secret is not found.
   */
  virtual const Ssl::TlsCertificateConfigSharedPtr secret() const PURE;

  virtual void addUpdateCallback(SecretCallbacks& callback) PURE;
  virtual void removeUpdateCallback(SecretCallbacks& callback) PURE;
};

typedef std::shared_ptr<DynamicSecretProvider> DynamicSecretProviderSharedPtr;

} // namespace Secret
} // namespace Envoy
