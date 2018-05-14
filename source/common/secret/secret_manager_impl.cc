#include "common/secret/secret_manager_impl.h"

#include <string>
#include <shared_mutex>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"

#include "envoy/server/instance.h"

#include "common/common/logger.h"
#include "common/secret/secret_impl.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Secret {

SecretManagerImpl::SecretManagerImpl(Server::Instance& server) {
}

bool SecretManagerImpl::addOrUpdateStaticSecret(const SecretPtr secret) {
  static_secrets_[secret->getName()] = secret;
  return true;
}

SecretPtr SecretManagerImpl::getStaticSecret(const std::string& name) {
  return (static_secrets_.find(name) != static_secrets_.end()) ? static_secrets_[name] : nullptr;
}

}  // namespace Secret
}  // namespace Envoy
