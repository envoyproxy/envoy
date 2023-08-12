#include "source/extensions/filters/network/wasm/config.h"

#include "envoy/extensions/filters/network/wasm/v3/wasm.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/common/empty_string.h"
#include "source/common/config/datasource.h"
#include "source/extensions/common/wasm/wasm.h"
#include "source/extensions/filters/network/wasm/wasm_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Wasm {

Network::FilterFactoryCb WasmFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::wasm::v3::Wasm& proto_config,
    const Network::NetworkFilterMatcherSharedPtr& network_filter_matcher,
    Server::Configuration::FactoryContext& context) {
  context.api().customStatNamespaces().registerStatNamespace(
      Extensions::Common::Wasm::CustomStatNamespace);
  auto filter_config = std::make_shared<FilterConfig>(proto_config, context);
  return [network_filter_matcher, filter_config](Network::FilterManager& filter_manager) -> void {
    auto filter = filter_config->createFilter();
    if (filter) {
      filter_manager.addFilter(network_filter_matcher, filter);
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
