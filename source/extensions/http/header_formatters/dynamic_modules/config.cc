#include "source/extensions/http/header_formatters/dynamic_modules/config.h"

#include <string>
#include <utility>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace DynamicModules {

using ProtoDynamicModuleHeaderFormatter =
    envoy::extensions::http::header_formatters::dynamic_modules::v3::DynamicModuleHeaderFormatter;

absl::StatusOr<Envoy::Http::StatefulHeaderKeyFormatterFactorySharedPtr>
DynamicModuleHeaderFormatterFactoryConfig::createFactoryFromProto(
    const Protobuf::Message& message, Server::Configuration::GenericFactoryContext& context) {
  const auto& proto_config =
      MessageUtil::downcastAndValidate<const ProtoDynamicModuleHeaderFormatter&>(
          message, ProtobufMessage::getStrictValidationVisitor());

  const auto& formatter_name = proto_config.header_formatter_name();

  // No init manager is passed, so the asynchronous remote-module path is unavailable and a remote
  // source that is not already cached is rejected below.
  auto load_result = Extensions::DynamicModules::newDynamicModuleByConfig(
      proto_config.dynamic_module_config(), formatter_name, context.serverFactoryContext());
  if (!load_result.ok()) {
    return load_result.status();
  }

  // Use knownAnyToBytes() to properly handle StringValue/BytesValue/Struct types.
  std::string formatter_config;
  if (proto_config.has_header_formatter_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.header_formatter_config());
    if (!config_or_error.ok()) {
      return absl::InvalidArgumentError("Failed to parse header formatter config: " +
                                        std::string(config_or_error.status().message()));
    }
    formatter_config = std::move(config_or_error.value());
  }

  // The configuration outlives the protocol options that reference it whenever a connection using
  // it is still draining, so the last reference is often dropped by a worker thread. It is handed
  // the main thread dispatcher to destroy itself on for that reason.
  auto config = newDynamicModuleHeaderFormatterConfig(
      formatter_name, formatter_config, std::move(load_result->loaded),
      context.serverFactoryContext().mainThreadDispatcher());
  if (!config.ok()) {
    // Keep the code, so a missing ABI symbol stays NotFound rather than becoming InvalidArgument.
    return absl::Status(config.status().code(), "Failed to create header formatter config: " +
                                                    std::string(config.status().message()));
  }

  return Envoy::Http::StatefulHeaderKeyFormatterFactorySharedPtr{std::move(config.value())};
}

REGISTER_FACTORY(DynamicModuleHeaderFormatterFactoryConfig,
                 Envoy::Http::StatefulHeaderKeyFormatterFactoryConfig);

} // namespace DynamicModules
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
