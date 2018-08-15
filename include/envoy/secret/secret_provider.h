#pragma once

#include "envoy/common/pure.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {
namespace Secret {

/**
 * A secret provider for each kind of secret.
 */
template <class SecretType> class SecretProvider {
public:
  virtual ~SecretProvider() {}

  /**
   * @return the secret. Returns nullptr if the secret is not ready.
   */
  virtual const SecretType* secret() const PURE;

  /**
   * Add secret callback into secret provider.
   * @param callback callback that is executed by secret provider.
   */
  virtual void addUpdateCallback(SecretCallbacks& callback) PURE;

  /**
   * Remove secret callback from secret provider.
   * @param callback callback that is executed by secret provider.
   */
  virtual void removeUpdateCallback(SecretCallbacks& callback) PURE;
};

typedef SecretProvider<Ssl::TlsCertificateConfig> TlsCertificateConfigProvider;
typedef std::shared_ptr<TlsCertificateConfigProvider> TlsCertificateConfigProviderSharedPtr;

} // namespace Secret
} // namespace Envoy
