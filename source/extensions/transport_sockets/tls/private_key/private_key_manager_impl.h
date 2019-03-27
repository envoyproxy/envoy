#pragma once

#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class PrivateKeyOperationsManagerImpl : public virtual Ssl::PrivateKeyOperationsManager {
public:
  Ssl::PrivateKeyOperationsProviderSharedPtr createPrivateKeyOperationsProvider(
      const envoy::api::v2::core::ConfigSource& config_source, const std::string& config_name,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) override;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
