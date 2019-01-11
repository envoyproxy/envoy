#include "test/mocks/secret/mocks.h"

#include "common/secret/secret_provider_impl.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Secret {

MockSecretManager::MockSecretManager() {
  ON_CALL(*this, createInlineTlsCertificateProvider(_))
      .WillByDefault(Invoke([](const envoy::api::v2::auth::TlsCertificate& tls_certificate) {
        return std::make_shared<Secret::TlsCertificateConfigProviderImpl>(tls_certificate);
      }));
  ON_CALL(*this, createInlineCertificateValidationContextProvider(_))
      .WillByDefault(Invoke([](const envoy::api::v2::auth::CertificateValidationContext&
                                   certificate_validation_context) {
        return std::make_shared<Secret::CertificateValidationContextConfigProviderImpl>(
            certificate_validation_context);
      }));
}

MockSecretManager::~MockSecretManager() {}

MockSecretCallbacks::MockSecretCallbacks() {}

MockSecretCallbacks::~MockSecretCallbacks() {}

} // namespace Secret
} // namespace Envoy
