#include "source/extensions/geoip_providers/maxmind/config.h"

#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/utility.h"

#include "geoip_provider.h"

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

MaxmindProviderFactory::MaxmindProviderFactory() : FactoryBase("envoy.geoip_providers.maxmind") {}

DriverSharedPtr MaxmindProviderFactory::createGeoipProviderDriverTyped(
    const envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  const auto& provider_config =
      std::make_shared<GeoipProviderConfig>(proto_config, stat_prefix, context.scope());
  return std::make_shared<GeoipProvider>(provider_config);
}

/**
 * Static registration for the Maxmind provider. @see RegisterFactory.
 */
REGISTER_FACTORY(MaxmindProviderFactory,
                 Geolocation::GeoipProviderFactory){"envoy.geoip_providers"};

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
