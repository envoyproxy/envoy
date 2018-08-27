#pragma once

#include <functional>

#include "envoy/common/callback.h"
#include "envoy/common/pure.h"
#include "envoy/ssl/certificate_validation_context_config.h"
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
   * Add secret update callback into secret provider.
   * It is safe to call this method by main thread and is safe to be invoked
   * on main thread.
   * @param callback callback that is executed by secret provider.
   * @return CallbackHandle the handle which can remove that update callback.
   */
  virtual Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) PURE;
};

typedef SecretProvider<Ssl::TlsCertificateConfig> TlsCertificateConfigProvider;
typedef std::shared_ptr<TlsCertificateConfigProvider> TlsCertificateConfigProviderSharedPtr;

typedef SecretProvider<Ssl::CertificateValidationContextConfig>
    CertificateValidationContextConfigProvider;
typedef std::shared_ptr<CertificateValidationContextConfigProvider>
    CertificateValidationContextConfigProviderSharedPtr;

} // namespace Secret
} // namespace Envoy
