#include "source/extensions/filters/http/geoip/config.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/geoip/geoip_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

namespace {
absl::Status validateConfig(const envoy::extensions::filters::http::geoip::v3::Geoip& config) {
  // xff_config and custom_header_config are mutually exclusive.
  if (config.has_xff_config() && config.has_custom_header_config()) {
    return absl::InvalidArgumentError(
        "Only one of xff_config or custom_header_config can be set in the geoip filter "
        "configuration");
  }
  return absl::OkStatus();
}
} // namespace

absl::StatusOr<Http::FilterFactoryCb> GeoipFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::geoip::v3::Geoip& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  // Validate configuration before creating the filter.
  auto status = validateConfig(proto_config);
  if (!status.ok()) {
    return status;
  }

  GeoipFilterConfigSharedPtr filter_config(
      std::make_shared<GeoipFilterConfig>(proto_config, stat_prefix, context.scope()));

  const auto& provider_config = proto_config.provider();
  auto& geo_provider_factory =
      Envoy::Config::Utility::getAndCheckFactory<Geolocation::GeoipProviderFactory>(
          provider_config);
  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateToFactoryConfig(
      provider_config, context.messageValidationVisitor(), geo_provider_factory);
  auto driver = geo_provider_factory.createGeoipProviderDriver(*message, stat_prefix, context);
  return [filter_config, driver](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<GeoipFilter>(filter_config, driver));
  };
}

/**
 * Static registration for geoip filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GeoipFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.geoip"};

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
