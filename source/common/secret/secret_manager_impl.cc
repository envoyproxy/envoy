#include "common/secret/secret_manager_impl.h"

#include "common/common/logger.h"
#include "common/secret/secret_impl.h"

namespace Envoy {
namespace Secret {

bool SecretManagerImpl::addOrUpdateStaticSecret(const SecretSharedPtr secret) {
  static_secrets_[secret->name()] = secret;
  return true;
}

SecretSharedPtr SecretManagerImpl::getStaticSecret(const std::string& name) {
  auto static_secret = static_secrets_.find(name);
  return (static_secret != static_secrets_.end()) ? static_secret->second : nullptr;
}

} // namespace Secret
} // namespace Envoy
