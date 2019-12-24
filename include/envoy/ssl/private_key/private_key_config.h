#pragma once

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/config/typed_config.h"
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
      const envoy::api::v2::auth::PrivateKeyProvider& config,
      Server::Configuration::TransportSocketFactoryContext& factory_context) PURE;

  const std::string category() const override { return "tls.key_providers"; };
};

} // namespace Ssl
} // namespace Envoy
