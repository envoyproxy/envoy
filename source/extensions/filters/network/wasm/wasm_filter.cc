#include "source/extensions/filters/network/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

FilterConfig::FilterConfig(const envoy::extensions::filters::network::wasm::v3::Wasm& config,
                           Server::Configuration::FactoryContext& context)
    : tls_slot_(ThreadLocal::TypedSlot<Common::Wasm::PluginHandleSharedPtrThreadLocal>::makeUnique(
          context.serverFactoryContext().threadLocal())) {
  const auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config.config(), context.listenerInfo().direction(),
      context.serverFactoryContext().localInfo(), &context.listenerInfo().metadata());

  auto callback = [plugin, this](Common::Wasm::WasmHandleSharedPtr base_wasm) {
    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    tls_slot_->set([base_wasm, plugin](Event::Dispatcher& dispatcher) {
      return std::make_shared<PluginHandleSharedPtrThreadLocal>(
          Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm, plugin, dispatcher));
    });
  };

  if (!Common::Wasm::createWasm(
          plugin, context.scope().createScope(""), context.serverFactoryContext().clusterManager(),
          context.initManager(), context.serverFactoryContext().mainThreadDispatcher(),
          context.serverFactoryContext().api(), context.serverFactoryContext().lifecycleNotifier(),
          remote_data_provider_, std::move(callback))) {
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm network filter {}", plugin->name_));
  }
}

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
