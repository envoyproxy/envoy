#include "test/mocks/secret/mocks.h"

#include "common/secret/secret_provider_impl.h"

using testing::Invoke;
using testing::_;

namespace Envoy {
namespace Secret {

MockSecretManager::MockSecretManager() {
  ON_CALL(*this, createInlineTlsCertificateProvider(_))
      .WillByDefault(Invoke([](const envoy::api::v2::auth::TlsCertificate& tls_certificate) {
        return std::make_shared<Secret::TlsCertificateConfigProviderImpl>(tls_certificate);
      }));
}

MockSecretManager::~MockSecretManager() {}

} // namespace Secret
} // namespace Envoy
