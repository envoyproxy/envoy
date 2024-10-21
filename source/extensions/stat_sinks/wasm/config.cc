#include "source/extensions/stat_sinks/wasm/config.h"

#include <memory>

#include "envoy/extensions/stat_sinks/wasm/v3/wasm.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/stat_sinks/wasm/wasm_stat_sink_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Wasm {

Stats::SinkPtr
WasmSinkFactory::createStatsSink(const Protobuf::Message& proto_config,
                                 Server::Configuration::ServerFactoryContext& context) {
  const auto& config =
      MessageUtil::downcastAndValidate<const envoy::extensions::stat_sinks::wasm::v3::Wasm&>(
          proto_config, context.messageValidationContext().staticValidationVisitor());

  auto plugin_config = std::make_unique<Common::Wasm::PluginConfig>(
      config.config(), context, context.scope(), context.initManager(),
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, nullptr, true);

  context.api().customStatNamespaces().registerStatNamespace(
      Extensions::Common::Wasm::CustomStatNamespace);

  return std::make_unique<WasmStatSink>(std::move(plugin_config));
}

ProtobufTypes::MessagePtr WasmSinkFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::stat_sinks::wasm::v3::Wasm>();
}

std::string WasmSinkFactory::name() const { return WasmName; }

/**
 * Static registration for the wasm access log. @see RegisterFactory.
 */
REGISTER_FACTORY(WasmSinkFactory, Server::Configuration::StatsSinkFactory);

} // namespace Wasm
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
