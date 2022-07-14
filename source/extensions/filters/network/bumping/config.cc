#include "source/extensions/filters/network/bumping/config.h"

#include "envoy/extensions/filters/network/bumping/v3/bumping.pb.h"
#include "envoy/extensions/filters/network/bumping/v3/bumping.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/bumping/bumping.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Bumping {

Network::FilterFactoryCb ConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::bumping::v3::Bumping& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  Envoy::Bumping::ConfigSharedPtr filter_config(
      std::make_shared<Envoy::Bumping::Config>(proto_config, context));
  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<Envoy::Bumping::Filter>(filter_config, context.clusterManager()));
  };
}

/**
 * Static registration for the bumping filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory){"envoy.bumping"};

} // namespace Bumping
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
