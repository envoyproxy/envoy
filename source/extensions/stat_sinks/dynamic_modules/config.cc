#include "source/extensions/stat_sinks/dynamic_modules/config.h"

#include <memory>

#include "envoy/extensions/stat_sinks/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

absl::StatusOr<Stats::SinkPtr> DynamicModuleStatsSinkFactory::createStatsSink(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& server) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink&>(
      config, server.messageValidationContext().staticValidationVisitor());

  const auto& module_config = proto_config.dynamic_module_config();

  auto dynamic_module_or_error = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!dynamic_module_or_error.ok()) {
    return absl::InvalidArgumentError("Failed to load dynamic module: " +
                                      std::string(dynamic_module_or_error.status().message()));
  }

  std::string sink_config_str;
  if (proto_config.has_sink_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.sink_config());
    if (!config_or_error.ok()) {
      return absl::InvalidArgumentError("Failed to parse sink config: " +
                                        std::string(config_or_error.status().message()));
    }
    sink_config_str = std::move(config_or_error.value());
  }

  auto sink_config =
      newDynamicModuleStatsSinkConfig(proto_config.sink_name(), sink_config_str,
                                      std::move(dynamic_module_or_error.value()), server);
  if (!sink_config.ok()) {
    return sink_config.status();
  }

  return std::make_unique<DynamicModuleStatsSink>(std::move(sink_config.value()));
}

ProtobufTypes::MessagePtr DynamicModuleStatsSinkFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::stat_sinks::dynamic_modules::v3::DynamicModuleStatsSink>();
}

REGISTER_FACTORY(DynamicModuleStatsSinkFactory, Server::Configuration::StatsSinkFactory);

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
