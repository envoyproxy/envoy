#include "common/secret/secret_manager_impl.h"

#include "common/common/logger.h"
#include "common/secret/secret_impl.h"

namespace Envoy {
namespace Secret {

bool SecretManagerImpl::addOrUpdateStaticSecret(const SecretSharedPtr secret) {
  static_secrets_[secret->getName()] = secret;
  return true;
}

SecretSharedPtr SecretManagerImpl::getStaticSecret(const std::string& name) {
  return (static_secrets_.find(name) != static_secrets_.end()) ? static_secrets_[name] : nullptr;
}

} // namespace Secret
} // namespace Envoy
