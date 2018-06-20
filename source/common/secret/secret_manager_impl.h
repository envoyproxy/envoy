#pragma once

#include <shared_mutex>
#include <unordered_map>

#include "envoy/secret/secret_manager.h"
#include "envoy/server/instance.h"
#include "envoy/ssl/tls_certificate_config.h"

#include "common/common/logger.h"
#include "common/secret/sds_api.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager, Logger::Loggable<Logger::Id::upstream> {
public:
  SecretManagerImpl(Server::Instance& server) : server_(server) {}

  void addOrUpdateSecret(const std::string& config_source_hash,
                         const envoy::api::v2::auth::Secret& secret) override;
  const Ssl::TlsCertificateConfig* findTlsCertificate(const std::string& config_source_hash,
                                                      const std::string& name) const override;
  std::string addOrUpdateSdsService(const envoy::api::v2::core::ConfigSource& config_source,
                                    std::string config_name) override;

private:
  Server::Instance& server_;
  // map hash code of SDS config source and SdsApi object.
  std::unordered_map<std::string, SdsApiPtr> sds_apis_;
  mutable std::shared_timed_mutex sds_api_mutex_;

  // Manages pairs of name and Ssl::TlsCertificateConfig grouped by SDS config source hash.
  // If SDS config source hash is empty, it is a static secret.
  std::unordered_map<std::string, std::unordered_map<std::string, Ssl::TlsCertificateConfigPtr>>
      tls_certificate_secrets_;
  mutable std::shared_timed_mutex tls_certificate_secrets_mutex_;
};

} // namespace Secret
} // namespace Envoy
