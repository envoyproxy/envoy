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

absl::StatusOr<std::unique_ptr<ThreadLocalGenericSecretProvider>>
ThreadLocalGenericSecretProvider::create(GenericSecretConfigProviderSharedPtr&& provider,
                                         ThreadLocal::SlotAllocator& tls, Api::Api& api) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<ThreadLocalGenericSecretProvider>(
      new ThreadLocalGenericSecretProvider(std::move(provider), tls, api, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}
ThreadLocalGenericSecretProvider::ThreadLocalGenericSecretProvider(
    GenericSecretConfigProviderSharedPtr&& provider, ThreadLocal::SlotAllocator& tls, Api::Api& api,
    absl::Status& creation_status)
    : provider_(provider), api_(api),
      tls_(std::make_unique<ThreadLocal::TypedSlot<ThreadLocalSecret>>(tls)),
      cb_(provider_->addUpdateCallback([this] { return update(); })) {
  std::string value;
  if (const auto* secret = provider_->secret(); secret != nullptr) {
    auto value_or_error = Config::DataSource::read(secret->secret(), true, api_);
    SET_AND_RETURN_IF_NOT_OK(value_or_error.status(), creation_status);
    value = std::move(value_or_error.value());
  }
  tls_->set([value = std::move(value)](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalSecret>(value);
  });
}

const std::string& ThreadLocalGenericSecretProvider::secret() const { return (*tls_)->value_; }

// This function is executed on the main during xDS update and can throw.
absl::Status ThreadLocalGenericSecretProvider::update() {
  std::string value;
  if (const auto* secret = provider_->secret(); secret != nullptr) {
    auto value_or_error = Config::DataSource::read(secret->secret(), true, api_);
    RETURN_IF_NOT_OK_REF(value_or_error.status());
    value = std::move(value_or_error.value());
  }
  tls_->runOnAllThreads(
      [value = std::move(value)](OptRef<ThreadLocalSecret> tls) { tls->value_ = value; });
  return absl::OkStatus();
}

} // namespace Secret
} // namespace Envoy
