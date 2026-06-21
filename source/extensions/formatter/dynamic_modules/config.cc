#include "source/extensions/formatter/dynamic_modules/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/formatter/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/formatter/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/formatter/dynamic_modules/formatter.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
namespace DynamicModules {

::Envoy::Formatter::CommandParserPtr DynamicModuleFormatterFactory::createCommandParserFromProto(
    const Protobuf::Message& config, Server::Configuration::GenericFactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::formatter::dynamic_modules::v3::DynamicModuleFormatter&>(
      config, context.messageValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module_or_error = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module_or_error.ok()) {
    throw EnvoyException("Failed to load dynamic module: " +
                         std::string(dynamic_module_or_error.status().message()));
  }

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string formatter_config_str;
  if (proto_config.has_formatter_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.formatter_config());
    if (!config_or_error.ok()) {
      throw EnvoyException("Failed to parse formatter config: " +
                           std::string(config_or_error.status().message()));
    }
    formatter_config_str = std::move(config_or_error.value());
  }

  auto formatter_config =
      newDynamicModuleFormatterConfig(proto_config.formatter_name(), formatter_config_str,
                                      std::move(dynamic_module_or_error.value()));
  if (!formatter_config.ok()) {
    throw EnvoyException("Failed to create formatter config: " +
                         std::string(formatter_config.status().message()));
  }

  return std::make_unique<DynamicModuleCommandParser>(std::move(formatter_config.value()));
}

ProtobufTypes::MessagePtr DynamicModuleFormatterFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::formatter::dynamic_modules::v3::DynamicModuleFormatter>();
}

std::string DynamicModuleFormatterFactory::name() const {
  return "envoy.formatter.dynamic_modules";
}

REGISTER_FACTORY(DynamicModuleFormatterFactory, ::Envoy::Formatter::CommandParserFactory);

} // namespace DynamicModules
} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
