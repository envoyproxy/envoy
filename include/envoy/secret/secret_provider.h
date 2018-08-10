#pragma once

#include "envoy/common/pure.h"
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

  // TODO(lizan): Add more methods for dynamic secret provider.
};

typedef SecretProvider<Ssl::TlsCertificateConfig> TlsCertificateConfigProvider;
typedef std::shared_ptr<TlsCertificateConfigProvider> TlsCertificateConfigProviderSharedPtr;

} // namespace Secret
} // namespace Envoy
