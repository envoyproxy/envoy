#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/ssl/private_key/private_key.h"

namespace Envoy {
namespace Ssl {

// Base class which the private key operation provider implementations can register.

class PrivateKeyMethodProviderInstanceFactory : public Config::UntypedFactory {
public:
  virtual ~PrivateKeyMethodProviderInstanceFactory() = default;

  /**
   * Create a particular PrivateKeyMethodProvider implementation. If the implementation is
   * unable to produce a PrivateKeyMethodProvider with the provided parameters, it should throw
   * an EnvoyException. The returned pointer should always be valid.
   * @param config supplies the custom proto configuration for the PrivateKeyMethodProvider
   * @param context supplies the factory context
   */
  virtual PrivateKeyMethodProviderSharedPtr createPrivateKeyMethodProviderInstance(
      const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context) PURE;

  std::string category() const override { return "envoy.tls.key_providers"; };
};

} // namespace Ssl
} // namespace Envoy
