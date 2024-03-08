#pragma once

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class PrivateKeyMethodManagerImpl : public virtual Ssl::PrivateKeyMethodManager {
public:
  // Ssl::PrivateKeyMethodManager
  Ssl::PrivateKeyMethodProviderSharedPtr createPrivateKeyMethodProvider(
      const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context) override;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
