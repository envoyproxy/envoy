#pragma once

#include "envoy/server/bootstrap_extension_config.h"

#include "source/extensions/bootstrap/dynamic_modules/extension.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

/**
 * Config registration for the dynamic modules bootstrap extension factory.
 */
class DynamicModuleBootstrapExtensionFactory
    : public Server::Configuration::BootstrapExtensionFactory {
public:
  std::string name() const override { return "envoy.bootstrap.dynamic_modules"; }

  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
