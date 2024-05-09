#include "source/common/secret/secret_provider_impl.h"

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/datasource.h"
#include "source/common/ssl/certificate_validation_context_config_impl.h"
#include "source/common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Secret {

TlsCertificateConfigProviderImpl::TlsCertificateConfigProviderImpl(
    const envoy::extensions::transport_sockets::tls::v3::TlsCertificate& tls_certificate)
    : tls_certificate_(
          std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsCertificate>(
              tls_certificate)) {}

CertificateValidationContextConfigProviderImpl::CertificateValidationContextConfigProviderImpl(
    const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
        certificate_validation_context)
    : certificate_validation_context_(
          std::make_unique<
              envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>(
              certificate_validation_context)) {}

TlsSessionTicketKeysConfigProviderImpl::TlsSessionTicketKeysConfigProviderImpl(
    const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys&
        tls_session_ticket_keys)
    : tls_session_ticket_keys_(
          std::make_unique<envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys>(
              tls_session_ticket_keys)) {}

GenericSecretConfigProviderImpl::GenericSecretConfigProviderImpl(
    const envoy::extensions::transport_sockets::tls::v3::GenericSecret& generic_secret)
    : generic_secret_(
          std::make_unique<envoy::extensions::transport_sockets::tls::v3::GenericSecret>(
              generic_secret)) {}

ThreadLocalGenericSecretProvider::ThreadLocalGenericSecretProvider(
    GenericSecretConfigProviderSharedPtr&& provider, ThreadLocal::SlotAllocator& tls, Api::Api& api)
    : provider_(provider), api_(api),
      tls_(std::make_unique<ThreadLocal::TypedSlot<ThreadLocalSecret>>(tls)),
      cb_(provider_->addUpdateCallback([this] {
        update();
        return absl::OkStatus();
      })) {
  std::string value;
  if (const auto* secret = provider_->secret(); secret != nullptr) {
    value =
        THROW_OR_RETURN_VALUE(Config::DataSource::read(secret->secret(), true, api_), std::string);
  }
  tls_->set([value = std::move(value)](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalSecret>(value);
  });
}

const std::string& ThreadLocalGenericSecretProvider::secret() const { return (*tls_)->value_; }

// This function is executed on the main during xDS update and can throw.
void ThreadLocalGenericSecretProvider::update() {
  std::string value;
  if (const auto* secret = provider_->secret(); secret != nullptr) {
    value =
        THROW_OR_RETURN_VALUE(Config::DataSource::read(secret->secret(), true, api_), std::string);
  }
  tls_->runOnAllThreads(
      [value = std::move(value)](OptRef<ThreadLocalSecret> tls) { tls->value_ = value; });
}

} // namespace Secret
} // namespace Envoy
