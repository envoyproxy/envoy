#include "extensions/filters/network/find_and_replace/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/network/find_and_replace/find_and_replace.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace FindAndReplace {

Network::FilterFactoryCb FindAndReplaceConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::find_and_replace::v2alpha1::FindAndReplace& proto_config,
    Server::Configuration::FactoryContext&) {

  ConfigConstSharedPtr filter_config(std::make_shared<Config>(proto_config));

  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<Filter>(filter_config));
  };
}

/**
 * Static registration for the FindAndReplace filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<FindAndReplaceConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace FindAndReplace
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
