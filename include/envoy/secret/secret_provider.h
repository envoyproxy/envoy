#pragma once

#include <functional>

#include "envoy/common/callback.h"
#include "envoy/common/pure.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {
namespace Secret {

/**
 * A secret provider for each kind of secret.
 */
template <class SecretType> class SecretProvider {
public:
  virtual ~SecretProvider() = default;

  /**
   * @return the secret. Returns nullptr if the secret is not ready.
   */
  virtual const SecretType* secret() const PURE;

  /**
   * Add secret validation callback into secret provider.
   * It is safe to call this method by main thread and callback is safe to be invoked
   * on main thread.
   * @param callback callback that is executed by secret provider.
   * @return CallbackHandle the handle which can remove that validation callback.
   */
  virtual Common::CallbackHandle*
  addValidationCallback(std::function<void(const SecretType&)> callback) PURE;

  /**
   * Add secret update callback into secret provider.
   * It is safe to call this method by main thread and callback is safe to be invoked
   * on main thread.
   * @param callback callback that is executed by secret provider.
   * @return CallbackHandle the handle which can remove that update callback.
   */
  virtual Common::CallbackHandle* addUpdateCallback(std::function<void()> callback) PURE;
};

using TlsCertificatePtr =
    std::unique_ptr<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>;
using CertificateValidationContextPtr =
    std::unique_ptr<envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>;
using TlsSessionTicketKeysPtr =
    std::unique_ptr<envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys>;
using GenericSecretPtr =
    std::unique_ptr<envoy::extensions::transport_sockets::tls::v3::GenericSecret>;

using TlsCertificateConfigProvider =
    SecretProvider<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>;
using TlsCertificateConfigProviderSharedPtr = std::shared_ptr<TlsCertificateConfigProvider>;

using CertificateValidationContextConfigProvider =
    SecretProvider<envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>;
using CertificateValidationContextConfigProviderSharedPtr =
    std::shared_ptr<CertificateValidationContextConfigProvider>;

using TlsSessionTicketKeysConfigProvider =
    SecretProvider<envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys>;
using TlsSessionTicketKeysConfigProviderSharedPtr =
    std::shared_ptr<TlsSessionTicketKeysConfigProvider>;

using GenericSecretConfigProvider =
    SecretProvider<envoy::extensions::transport_sockets::tls::v3::GenericSecret>;
using GenericSecretConfigProviderSharedPtr = std::shared_ptr<GenericSecretConfigProvider>;

} // namespace Secret
} // namespace Envoy
