#pragma once

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class PrivateKeyOperationsManagerImpl : public virtual Ssl::PrivateKeyOperationsManager {
public:
  // Ssl::PrivateKeyOperationsManager
  Ssl::PrivateKeyOperationsProviderSharedPtr createPrivateKeyOperationsProvider(
      const envoy::api::v2::auth::PrivateKeyOperations& message,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) override;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
