#pragma once

#include "envoy/registry/registry.h"
#include "envoy/server/bootstrap_extension_config.h"

#include "source/extensions/wildcard/virtual_ip_cache/cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace VirtualIp {

/**
 * Config registration for the Virtual IP Cache Manager bootstrap extension.
 */
class VirtualIpCacheConfigFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  // BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;
};

} // namespace VirtualIp
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy
