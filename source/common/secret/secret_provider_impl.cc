#include "common/secret/secret_provider_impl.h"

#include "envoy/extensions/transport_sockets/tls/v3alpha/cert.pb.h"

#include "common/common/assert.h"
#include "common/ssl/certificate_validation_context_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

TlsCertificateConfigProviderImpl::TlsCertificateConfigProviderImpl(
    const envoy::extensions::transport_sockets::tls::v3alpha::TlsCertificate& tls_certificate)
    : tls_certificate_(
          std::make_unique<envoy::extensions::transport_sockets::tls::v3alpha::TlsCertificate>(
              tls_certificate)) {}

CertificateValidationContextConfigProviderImpl::CertificateValidationContextConfigProviderImpl(
    const envoy::extensions::transport_sockets::tls::v3alpha::CertificateValidationContext&
        certificate_validation_context)
    : certificate_validation_context_(
          std::make_unique<
              envoy::extensions::transport_sockets::tls::v3alpha::CertificateValidationContext>(
              certificate_validation_context)) {}

TlsSessionTicketKeysConfigProviderImpl::TlsSessionTicketKeysConfigProviderImpl(
    const envoy::extensions::transport_sockets::tls::v3alpha::TlsSessionTicketKeys&
        tls_session_ticket_keys)
    : tls_session_ticket_keys_(
          std::make_unique<
              envoy::extensions::transport_sockets::tls::v3alpha::TlsSessionTicketKeys>(
              tls_session_ticket_keys)) {}

} // namespace Secret
} // namespace Envoy
