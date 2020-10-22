#include "extensions/filters/network/wasm/config.h"

#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/common/empty_string.h"
#include "common/config/datasource.h"

#include "extensions/common/wasm/wasm.h"
#include "extensions/filters/network/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

Network::FilterFactoryCb WasmFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::wasm::v3::Wasm& proto_config,
    Server::Configuration::FactoryContext& context) {
  auto filter_config = std::make_shared<FilterConfig>(proto_config, context);
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    auto filter = filter_config->createFilter();
    if (filter) {
      filter_manager.addFilter(filter);
    } // else fail open
  };
}

/**
 * Static registration for the Wasm filter. @see RegisterFactory.
 */
REGISTER_FACTORY(WasmFilterConfig, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace Wasm
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
