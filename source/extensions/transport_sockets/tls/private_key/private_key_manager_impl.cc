#include "extensions/transport_sockets/tls/private_key/private_key_manager_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

Envoy::Ssl::PrivateKeyOperationsProviderSharedPtr
PrivateKeyOperationsManagerImpl::findPrivateKeyOperationsProvider(
    const envoy::api::v2::core::ConfigSource& config_source, const std::string& config_name) {

  (void)config_name;
  (void)config_source;

  // TODO(ipuustin): implement this.

  return nullptr;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
