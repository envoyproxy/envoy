#pragma once

#include "envoy/api/v2/core/config_source.pb.h"
#include "envoy/ssl/private_key/private_key.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class PrivateKeyOperationsManagerImpl : public virtual Ssl::PrivateKeyOperationsManager {
public:
  Ssl::PrivateKeyOperationsProviderSharedPtr
  findPrivateKeyOperationsProvider(const envoy::api::v2::core::ConfigSource& config_source,
                                   const std::string& config_name) override;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
