#include "source/extensions/filters/network/geoip/config.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/geoip/geoip_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Geoip {

absl::StatusOr<Network::FilterFactoryCb> GeoipFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::geoip::v3::Geoip& proto_config,
    Server::Configuration::FactoryContext& context) {
  const std::string& stat_prefix = proto_config.stat_prefix();
  GeoipFilterConfigSharedPtr filter_config(
      std::make_shared<GeoipFilterConfig>(proto_config, stat_prefix, context.scope()));

  const auto& provider_config = proto_config.provider();
  auto& geo_provider_factory =
      Envoy::Config::Utility::getAndCheckFactory<Geolocation::GeoipProviderFactory>(
          provider_config);
  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateToFactoryConfig(
      provider_config, context.messageValidationVisitor(), geo_provider_factory);
  auto driver = geo_provider_factory.createGeoipProviderDriver(*message, stat_prefix, context);

  return [filter_config, driver](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<GeoipFilter>(filter_config, driver));
  };
}

/**
 * Static registration for geoip network filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GeoipFilterFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
