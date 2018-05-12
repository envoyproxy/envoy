#pragma once

#include <shared_mutex>
#include <unordered_map>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"

#include "envoy/server/instance.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret.h"

#include "common/common/logger.h"
#include "common/secret/secret_impl.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager, Logger::Loggable<Logger::Id::upstream> {
 public:
  SecretManagerImpl(Server::Instance& server, envoy::config::bootstrap::v2::SecretManager config);

  virtual ~SecretManagerImpl() {
  }

  bool addOrUpdateStaticSecret(const SecretPtr secret) override;
  SecretPtr getStaticSecret(const std::string& name) override;

 private:
  Server::Instance& server_;
  SecretPtrMap static_secrets_;

  envoy::config::bootstrap::v2::SecretManager config_;
};

}  // namespace Secret
}  // namespace Envoy
