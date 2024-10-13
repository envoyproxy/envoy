#include "source/extensions/filters/network/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

FilterConfig::FilterConfig(const envoy::extensions::filters::network::wasm::v3::Wasm& config,
                           Server::Configuration::FactoryContext& context)
    : Extensions::Common::Wasm::PluginConfig(
          config.config(), context.serverFactoryContext(), context.scope(), context.initManager(),
          context.listenerInfo().direction(), &context.listenerInfo().metadata(), false) {}

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
