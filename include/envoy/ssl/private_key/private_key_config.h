#pragma once

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/ssl/private_key/private_key.h"

namespace Envoy {
namespace Ssl {

// Base class which the private key operation provider implementations can register.

class PrivateKeyOperationsProviderInstanceFactory {
public:
  virtual ~PrivateKeyOperationsProviderInstanceFactory() {}
  virtual PrivateKeyOperationsProviderSharedPtr createPrivateKeyOperationsProviderInstance(
      const envoy::api::v2::auth::PrivateKeyOperations& message,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) PURE;
  virtual std::string name() const PURE;
};

} // namespace Ssl
} // namespace Envoy
