#include "extensions/filters/network/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

FilterConfig::FilterConfig(const envoy::extensions::filters::network::wasm::v3::Wasm& config,
                           Server::Configuration::FactoryContext& context)
    : base_config_(new Envoy::Extensions::Common::Wasm::WasmBaseConfig(config.config())),
      tls_slot_(
          ThreadLocal::TypedSlot<Common::Wasm::PluginHandle>::makeUnique(context.threadLocal())) {
  plugin_ =
      std::make_shared<Common::Wasm::Plugin>(base_config_->config(), context.direction(),
                                             context.localInfo(), &context.listenerMetadata());

  auto plugin = plugin_;
  auto callback = [plugin, this](Common::Wasm::WasmHandleSharedPtr base_wasm) {
    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    tls_slot_->set([base_wasm, plugin](Event::Dispatcher& dispatcher) {
      return Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm, plugin, dispatcher);
    });
  };

  if (!Common::Wasm::createWasm(*base_config_, plugin_, context.scope().createScope(""),
                                context.clusterManager(), context.initManager(),
                                context.dispatcher(), context.api(), context.lifecycleNotifier(),
                                remote_data_provider_, std::move(callback))) {
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm network filter {}", plugin->name_));
  }
}

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
