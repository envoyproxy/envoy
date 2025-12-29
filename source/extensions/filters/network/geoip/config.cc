#include "source/extensions/filters/network/geoip/config.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/formatter/substitution_formatter.h"
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

  // Create client IP formatter if configured.
  Formatter::FormatterConstSharedPtr client_ip_formatter;
  if (!proto_config.client_ip().empty()) {
    auto formatter_or_error = Formatter::FormatterImpl::create(proto_config.client_ip(), false);
    if (!formatter_or_error.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("Failed to parse client_ip: {}", formatter_or_error.status().message()));
    }
    client_ip_formatter = std::move(formatter_or_error.value());
  }

  GeoipFilterConfigSharedPtr filter_config(std::make_shared<GeoipFilterConfig>(
      proto_config, stat_prefix, context.scope(), std::move(client_ip_formatter)));

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
