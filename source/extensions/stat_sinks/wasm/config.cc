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

  auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config.config(), envoy::config::core::v3::TrafficDirection::UNSPECIFIED, context.localInfo(),
      nullptr);

  auto wasm_sink = std::make_unique<WasmStatSink>(plugin, nullptr);

  auto callback = [&wasm_sink, &context, plugin](Common::Wasm::WasmHandleSharedPtr base_wasm) {
    if (!base_wasm) {
      if (plugin->fail_open_) {
        ENVOY_LOG(error, "Unable to create Wasm Stat Sink {}", plugin->name_);
      } else {
        ENVOY_LOG(critical, "Unable to create Wasm Stat Sink {}", plugin->name_);
      }
      return;
    }
    wasm_sink->setSingleton(Common::Wasm::getOrCreateThreadLocalPlugin(
        base_wasm, plugin, context.mainThreadDispatcher()));
  };

  if (!Common::Wasm::createWasm(plugin, context.scope().createScope(""), context.clusterManager(),
                                context.initManager(), context.mainThreadDispatcher(),
                                context.api(), context.lifecycleNotifier(), remote_data_provider_,
                                std::move(callback))) {
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm Stat Sink {}", plugin->name_));
  }

  context.api().customStatNamespaces().registerStatNamespace(
      Extensions::Common::Wasm::CustomStatNamespace);
  return wasm_sink;
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
