#pragma once

#include <unordered_map>

#include "envoy/secret/secret.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/server/instance.h"

#include "common/common/logger.h"
#include "common/secret/secret_impl.h"

namespace Envoy {
namespace Secret {

class SecretManagerImpl : public SecretManager, Logger::Loggable<Logger::Id::upstream> {
public:
  SecretManagerImpl(){};

  virtual ~SecretManagerImpl() {}

  bool addOrUpdateStaticSecret(const SecretSharedPtr secret) override;
  const SecretSharedPtr staticSecret(const std::string& name) const override;

private:
  SecretSharedPtrMap static_secrets_;
};

} // namespace Secret
} // namespace Envoy
