#include "source/extensions/filters/network/sni_to_metadata/config.h"

#include "source/extensions/filters/network/sni_to_metadata/filter.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SniToMetadata {

Network::FilterFactoryCb SniToMetadataFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::sni_to_metadata::v3::SniToMetadataFilter& config,
    Server::Configuration::FactoryContext& context) {

  ConfigSharedPtr filter_config =
      std::make_shared<Config>(config, context.serverFactoryContext().regexEngine());

  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(filter_config));
  };
}

REGISTER_FACTORY(SniToMetadataFilterFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace SniToMetadata
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy