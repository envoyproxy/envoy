#pragma once

#include <functional>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/certificate_validation_context_config.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {
namespace Secret {

class TlsCertificateConfigProviderImpl : public TlsCertificateConfigProvider {
public:
  TlsCertificateConfigProviderImpl(const envoy::api::v2::auth::TlsCertificate& tls_certificate);

  const envoy::api::v2::auth::TlsCertificate* secret() const override {
    return tls_certificate_.get();
  }

  Common::CallbackHandle* addUpdateCallback(std::function<void()>) override { return nullptr; }

private:
  Secret::TlsCertificatePtr tls_certificate_;
};

class CertificateValidationContextConfigProviderImpl
    : public CertificateValidationContextConfigProvider {
public:
  CertificateValidationContextConfigProviderImpl(
      const envoy::api::v2::auth::CertificateValidationContext& certificate_validation_context);

  const envoy::api::v2::auth::CertificateValidationContext* secret() const override {
    return certificate_validation_context_.get();
  }

  Common::CallbackHandle* addUpdateCallback(std::function<void()>) override { return nullptr; }

private:
  Secret::CertificateValidationContextPtr certificate_validation_context_;
};

} // namespace Secret
} // namespace Envoy
