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

  // When `module.local.filename` is set, load the module from that explicit
  // path. Otherwise fall back to the name-based search ($ENVOY_DYNAMIC_MODULES_SEARCH_PATH
  // / lib<name>.so). `module.remote` is intentionally not supported here — stats
  // sinks are a process-wide bootstrap resource and fetching a remote .so at
  // server start without an init manager isn't a pattern we want to introduce.
  absl::StatusOr<Extensions::DynamicModules::DynamicModulePtr> dynamic_module_or_error;
  if (module_config.has_module()) {
    if (module_config.module().has_remote()) {
      return absl::InvalidArgumentError(
          "Remote module sources are not supported for dynamic_modules stats sinks; "
          "use dynamic_module_config.module.local.filename or dynamic_module_config.name");
    }
    if (!module_config.module().has_local() || !module_config.module().local().has_filename()) {
      return absl::InvalidArgumentError(
          "dynamic_module_config.module must specify module.local.filename");
    }
    dynamic_module_or_error = Extensions::DynamicModules::newDynamicModule(
        module_config.module().local().filename(), module_config.do_not_close(),
        module_config.load_globally());
  } else {
    if (module_config.name().empty()) {
      return absl::InvalidArgumentError(
          "Either 'name' or 'module' must be specified in dynamic_module_config");
    }
    dynamic_module_or_error = Extensions::DynamicModules::newDynamicModuleByName(
        module_config.name(), module_config.do_not_close(), module_config.load_globally());
  }

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

  auto sink_config = newDynamicModuleStatsSinkConfig(proto_config.sink_name(), sink_config_str,
                                                     std::move(dynamic_module_or_error.value()));
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
