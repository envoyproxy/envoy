#include "common/secret/secret_provider_impl.h"

#include "common/common/assert.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

TlsCertificateConfigProviderImpl::TlsCertificateConfigProviderImpl(
    const envoy::api::v2::auth::TlsCertificate& tls_certificate)
    : tls_certificate_(std::make_unique<Ssl::TlsCertificateConfigImpl>(tls_certificate)) {}

CertificateValidationContextConfigProviderImpl::CertificateValidationContextConfigProviderImpl(
    const envoy::api::v2::auth::CertificateValidationContext& certificate_validation_context)
    : certificate_validation_context_(std::make_unique<Ssl::CertificateValidationContextConfigImpl>(
          certificate_validation_context)) {}

} // namespace Secret
} // namespace Envoy
