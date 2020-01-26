#include "test/mocks/secret/mocks.h"

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "common/secret/secret_provider_impl.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Secret {

MockSecretManager::MockSecretManager() {
  ON_CALL(*this, createInlineTlsCertificateProvider(_))
      .WillByDefault(Invoke(
          [](const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& tls_certificate) {
            return std::make_shared<Secret::TlsCertificateConfigProviderImpl>(tls_certificate);
          }));
  ON_CALL(*this, createInlineCertificateValidationContextProvider(_))
      .WillByDefault(Invoke(
          [](const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
                 certificate_validation_context) {
            return std::make_shared<Secret::CertificateValidationContextConfigProviderImpl>(
                certificate_validation_context);
          }));
}

MockSecretManager::~MockSecretManager() = default;

MockSecretCallbacks::MockSecretCallbacks() = default;

MockSecretCallbacks::~MockSecretCallbacks() = default;

} // namespace Secret
} // namespace Envoy
