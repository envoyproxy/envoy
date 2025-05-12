#include "source/extensions/filters/http/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Wasm {

FilterConfig::FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
                           Server::Configuration::FactoryContext& context)
    : Extensions::Common::Wasm::PluginConfig(
          config.config(), context.serverFactoryContext(), context.scope(), context.initManager(),
          context.listenerInfo().direction(), &context.listenerInfo().metadata(), false) {}

FilterConfig::FilterConfig(const envoy::extensions::filters::http::wasm::v3::Wasm& config,
                           Server::Configuration::UpstreamFactoryContext& context)
    : Extensions::Common::Wasm::PluginConfig(
          config.config(), context.serverFactoryContext(), context.scope(), context.initManager(),
          envoy::config::core::v3::TrafficDirection::OUTBOUND, nullptr, false) {}

} // namespace Wasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
