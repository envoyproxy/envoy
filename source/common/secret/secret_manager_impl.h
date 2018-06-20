#pragma once

#include <unordered_map>

#include "envoy/secret/secret_manager.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager, Logger::Loggable<Logger::Id::upstream> {
public:
  void addOrUpdateSecret(const envoy::api::v2::auth::Secret& secret) override;
  const Ssl::TlsCertificateConfig* findTlsCertificate(const std::string& name) const override;

private:
  std::unordered_map<std::string, Ssl::TlsCertificateConfigPtr> tls_certificate_secrets_;
};

} // namespace Secret
} // namespace Envoy
