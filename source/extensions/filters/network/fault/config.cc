#include "source/extensions/filters/network/fault/config.h"

#include <memory>

#include "envoy/extensions/filters/network/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/network/fault/v3/fault.pb.validate.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/fault/fault_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Fault {

absl::StatusOr<Network::FilterFactoryCb> FaultFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::fault::v3::NetworkFault& proto_config,
    Server::Configuration::FactoryContext& context) {
  FaultFilterConfigSharedPtr filter_config = std::make_shared<FaultFilterConfig>(
      proto_config, context.scope(), context.serverFactoryContext().runtime());
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<FaultFilter>(filter_config));
  };
}

/**
 * Static registration for the network fault filter. @see RegisterFactory.
 */
REGISTER_FACTORY(FaultFilterFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace Fault
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
