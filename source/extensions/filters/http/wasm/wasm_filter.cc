#include "source/extensions/filters/http/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

FilterConfig::FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
                           Server::Configuration::FactoryContext& context) {
  auto& server = context.serverFactoryContext();
  const auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config.config(), context.listenerInfo().direction(), server.localInfo(),
      &context.listenerInfo().metadata());
  createWasm(plugin, server, context.scope().createScope(""), context.initManager());
}

FilterConfig::FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
                           Server::Configuration::UpstreamFactoryContext& context) {
  auto& server = context.serverFactoryContext();
  const auto plugin = std::make_shared<Common::Wasm::Plugin>(
      config.config(), envoy::config::core::v3::TrafficDirection::OUTBOUND, server.localInfo(),
      nullptr);
  createWasm(plugin, server, context.scope().createScope(""), context.initManager());
}

void FilterConfig::createWasm(PluginSharedPtr plugin,
                              Envoy::Server::Configuration::ServerFactoryContext& server,
                              const Stats::ScopeSharedPtr& scope,
                              Envoy::Init::Manager& init_manager) {
  tls_slot_ = ThreadLocal::TypedSlot<Common::Wasm::PluginHandleSharedPtrThreadLocal>::makeUnique(
      server.threadLocal());
  auto callback = [plugin, this](const Common::Wasm::WasmHandleSharedPtr& base_wasm) {
    // NB: the Slot set() call doesn't complete inline, so all arguments must outlive this call.
    tls_slot_->set([base_wasm, plugin](Event::Dispatcher& dispatcher) {
      return std::make_shared<PluginHandleSharedPtrThreadLocal>(
          Common::Wasm::getOrCreateThreadLocalPlugin(base_wasm, plugin, dispatcher));
    });
  };

  if (!Common::Wasm::createWasm(
          plugin, scope, server.clusterManager(), init_manager, server.mainThreadDispatcher(),
          server.api(), server.lifecycleNotifier(), remote_data_provider_, std::move(callback))) {
    throw Common::Wasm::WasmException(
        fmt::format("Unable to create Wasm HTTP filter {}", plugin->name_));
  }
}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
