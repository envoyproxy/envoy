#pragma once

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/tls_certificate_config.h"

namespace Envoy {
namespace Secret {

class TlsCertificateConfigProviderImpl : public TlsCertificateConfigProvider {
public:
  TlsCertificateConfigProviderImpl(const envoy::api::v2::auth::TlsCertificate& tls_certificate);

  const Ssl::TlsCertificateConfig* secret() const override { return tls_certificate_.get(); }

private:
  Ssl::TlsCertificateConfigPtr tls_certificate_;
};

} // namespace Secret
} // namespace Envoy
