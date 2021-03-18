#pragma once

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "source/common/common/logger.h"
#include "source/extensions/private_key_providers/cryptomb/cryptomb_private_key_provider.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {

class CryptoMbPrivateKeyMethodFactory : public Ssl::PrivateKeyMethodProviderInstanceFactory,
                                        public Logger::Loggable<Logger::Id::connection> {
public:
  // Ssl::PrivateKeyMethodProviderInstanceFactory
  Ssl::PrivateKeyMethodProviderSharedPtr createPrivateKeyMethodProviderInstance(
      const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& message,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context);
  std::string name() const { return "cryptomb"; };
};
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
