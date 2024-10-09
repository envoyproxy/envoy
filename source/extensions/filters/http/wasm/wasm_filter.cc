#include "source/extensions/filters/http/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

FilterConfig::FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
                           Server::Configuration::FactoryContext& context) {
  auto& server = context.serverFactoryContext();
  plugin_ = std::make_shared<Common::Wasm::Plugin>(
      config.config(), context.listenerInfo().direction(), server.localInfo(),
      &context.listenerInfo().metadata());
  createWasm(server, context.scope().createScope(""), context.initManager());
}

FilterConfig::FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
                           Server::Configuration::UpstreamFactoryContext& context) {
  auto& server = context.serverFactoryContext();
  plugin_ = std::make_shared<Common::Wasm::Plugin>(
      config.config(), envoy::config::core::v3::TrafficDirection::OUTBOUND, server.localInfo(),
      nullptr);
  createWasm(server, context.scope().createScope(""), context.initManager());
}

void FilterConfig::createWasm(Server::Configuration::ServerFactoryContext& server,
                              Stats::ScopeSharedPtr scope, Envoy::Init::Manager& init_manager) {
  tls_slot_ = ThreadLocal::TypedSlot<Common::Wasm::PluginHandleThreadLocal>::makeUnique(
      server.threadLocal());

  auto callback = [this](const Common::Wasm::WasmHandleSharedPtr& base_wasm) {
    // Keep the base wasm so we can reuse it if we need to reload the plugin WASM vm.
    base_wasm_ = base_wasm;

    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    tls_slot_->set([base_wasm, plugin = this->plugin_](Event::Dispatcher& dispatcher) {
      return std::make_shared<PluginHandleThreadLocal>(
          Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm, plugin, dispatcher));
    });
  };

  if (!Common::Wasm::createWasm(
          plugin_, scope, server.clusterManager(), init_manager, server.mainThreadDispatcher(),
          server.api(), server.lifecycleNotifier(), remote_data_provider_, std::move(callback))) {
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm HTTP filter {}", plugin_->name_));
  }
}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
