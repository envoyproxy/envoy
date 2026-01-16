#include "source/extensions/bootstrap/dynamic_modules/factory.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/bootstrap/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/bootstrap/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

Server::BootstrapExtensionPtr DynamicModuleBootstrapExtensionFactory::createBootstrapExtension(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) {

  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension&>(
      config, context.messageValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module.ok()) {
    throwEnvoyExceptionOrPanic("Failed to load dynamic module: " +
                               std::string(dynamic_module.status().message()));
  }

  std::string extension_config_str;
  if (proto_config.has_extension_config()) {
    auto config_or_error = MessageUtil::anyToBytes(proto_config.extension_config());
    if (!config_or_error.ok()) {
      throwEnvoyExceptionOrPanic("Failed to parse extension config: " +
                                 std::string(config_or_error.status().message()));
    }
    extension_config_str = std::move(config_or_error.value());
  }

  auto extension_config = newDynamicModuleBootstrapExtensionConfig(
      proto_config.extension_name(), extension_config_str, std::move(dynamic_module.value()),
      context.mainThreadDispatcher(), context, context.serverScope().store());

  if (!extension_config.ok()) {
    throwEnvoyExceptionOrPanic("Failed to create extension config: " +
                               std::string(extension_config.status().message()));
  }

  auto extension = std::make_unique<DynamicModuleBootstrapExtension>(extension_config.value());
  extension->initializeInModuleExtension();
  return extension;
}

ProtobufTypes::MessagePtr DynamicModuleBootstrapExtensionFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::bootstrap::dynamic_modules::v3::DynamicModuleBootstrapExtension>();
}

/**
 * Static registration for the dynamic modules bootstrap extension factory.
 */
REGISTER_FACTORY(DynamicModuleBootstrapExtensionFactory,
                 Server::Configuration::BootstrapExtensionFactory);

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
