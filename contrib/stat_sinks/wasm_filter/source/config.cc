#include "contrib/stat_sinks/wasm_filter/source/config.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "source/common/config/utility.h"
#include "source/extensions/common/wasm/wasm.h"

#include "contrib/envoy/extensions/stat_sinks/wasm_filter/v3/wasm_filter.pb.validate.h"
#include "contrib/stat_sinks/wasm_filter/source/wasm_filter_stat_sink_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace WasmFilter {

absl::StatusOr<Stats::SinkPtr>
WasmFilterSinkFactory::createStatsSink(const Protobuf::Message& proto_config,
                                       Server::Configuration::ServerFactoryContext& context) {
  const auto& config = MessageUtil::downcastAndValidate<
      const envoy::extensions::stat_sinks::wasm_filter::v3::WasmFilterStatsSinkConfig&>(
      proto_config, context.messageValidationContext().staticValidationVisitor());

  // Scope a TagVector for the plugin's onConfigure to write global tags into.
  // This avoids shared thread-local state when multiple wasm_filter sinks exist.
  Stats::TagVector startup_tags;
  setGlobalTags(&startup_tags);

  auto plugin_config = std::make_unique<Common::Wasm::PluginConfig>(
      config.wasm_config(), context, context.scope(), context.initManager(),
      envoy::config::core::v3::TrafficDirection::UNSPECIFIED, nullptr, true);

  setGlobalTags(nullptr);

  context.api().customStatNamespaces().registerStatNamespace(
      Extensions::Common::Wasm::CustomStatNamespace);

  const auto& inner_sink_config = config.inner_sink();
  auto& inner_factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::StatsSinkFactory>(
          inner_sink_config);
  ProtobufTypes::MessagePtr inner_message = Config::Utility::translateToFactoryConfig(
      inner_sink_config, context.messageValidationContext().staticValidationVisitor(),
      inner_factory);

  auto inner_sink = inner_factory.createStatsSink(*inner_message, context);
  RETURN_IF_NOT_OK_REF(inner_sink.status());

  return std::make_unique<WasmFilterStatsSink>(
      std::move(plugin_config), std::move(inner_sink.value()), context.scope().symbolTable(),
      std::move(startup_tags));
}

ProtobufTypes::MessagePtr WasmFilterSinkFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::stat_sinks::wasm_filter::v3::WasmFilterStatsSinkConfig>();
}

std::string WasmFilterSinkFactory::name() const { return WasmFilterName; }

REGISTER_FACTORY(WasmFilterSinkFactory, Server::Configuration::StatsSinkFactory);

} // namespace WasmFilter
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
