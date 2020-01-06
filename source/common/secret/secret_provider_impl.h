#pragma once

#include <functional>

#include "envoy/extensions/transport_sockets/tls/v3alpha/cert.pb.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {
namespace Secret {

class TlsCertificateConfigProviderImpl : public TlsCertificateConfigProvider {
public:
  TlsCertificateConfigProviderImpl(
      const envoy::extensions::transport_sockets::tls::v3alpha::TlsCertificate& tls_certificate);

  const envoy::extensions::transport_sockets::tls::v3alpha::TlsCertificate*
  secret() const override {
    return tls_certificate_.get();
  }

  Common::CallbackHandle* addValidationCallback(
      std::function<void(
          const envoy::extensions::transport_sockets::tls::v3alpha::TlsCertificate&)>) override {
    return nullptr;
  }

  Common::CallbackHandle* addUpdateCallback(std::function<void()>) override { return nullptr; }

private:
  Secret::TlsCertificatePtr tls_certificate_;
};

class CertificateValidationContextConfigProviderImpl
    : public CertificateValidationContextConfigProvider {
public:
  CertificateValidationContextConfigProviderImpl(
      const envoy::extensions::transport_sockets::tls::v3alpha::CertificateValidationContext&
          certificate_validation_context);

  const envoy::extensions::transport_sockets::tls::v3alpha::CertificateValidationContext*
  secret() const override {
    return certificate_validation_context_.get();
  }

  Common::CallbackHandle* addValidationCallback(
      std::function<void(
          const envoy::extensions::transport_sockets::tls::v3alpha::CertificateValidationContext&)>)
      override {
    return nullptr;
  }

  Common::CallbackHandle* addUpdateCallback(std::function<void()>) override { return nullptr; }

private:
  Secret::CertificateValidationContextPtr certificate_validation_context_;
};

class TlsSessionTicketKeysConfigProviderImpl : public TlsSessionTicketKeysConfigProvider {
public:
  TlsSessionTicketKeysConfigProviderImpl(
      const envoy::extensions::transport_sockets::tls::v3alpha::TlsSessionTicketKeys&
          tls_session_ticket_keys);

  const envoy::extensions::transport_sockets::tls::v3alpha::TlsSessionTicketKeys*
  secret() const override {
    return tls_session_ticket_keys_.get();
  }

  Common::CallbackHandle* addValidationCallback(
      std::function<
          void(const envoy::extensions::transport_sockets::tls::v3alpha::TlsSessionTicketKeys&)>)
      override {
    return nullptr;
  }

  Common::CallbackHandle* addUpdateCallback(std::function<void()>) override { return nullptr; }

private:
  Secret::TlsSessionTicketKeysPtr tls_session_ticket_keys_;
};

} // namespace Secret
} // namespace Envoy
