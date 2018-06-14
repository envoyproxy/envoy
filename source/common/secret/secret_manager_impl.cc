#include "common/secret/secret_manager_impl.h"

#include "envoy/common/exception.h"

#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

void SecretManagerImpl::addOrUpdateSecret(const envoy::api::v2::auth::Secret& secret) {
  switch (secret.type_case()) {
  case envoy::api::v2::auth::Secret::TypeCase::kTlsCertificate:
    tls_certificate_secrets_[secret.name()] =
        std::make_unique<Ssl::TlsCertificateConfigImpl>(secret.tls_certificate());
    break;
  default:
    throw EnvoyException("Secret type not implemented");
  }
}

const Ssl::TlsCertificateConfig*
SecretManagerImpl::findTlsCertificate(const std::string& name) const {
  auto secret = tls_certificate_secrets_.find(name);
  return (secret != tls_certificate_secrets_.end()) ? secret->second.get() : nullptr;
}

} // namespace Secret
} // namespace Envoy
