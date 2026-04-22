#pragma once

#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.h"
#include "envoy/extensions/filters/network/geoip/v3/geoip.pb.validate.h"
#include "envoy/geoip/geoip_provider_driver.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Geoip {

/**
 * Config registration for the geoip network filter. @see NamedNetworkFilterConfigFactory.
 */
class GeoipFilterFactory : public Common::ExceptionFreeFactoryBase<
                               envoy::extensions::filters::network::geoip::v3::Geoip> {
public:
  GeoipFilterFactory() : ExceptionFreeFactoryBase("envoy.filters.network.geoip") {}

private:
  absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::geoip::v3::Geoip& proto_config,
      Server::Configuration::FactoryContext& context) override;

  bool isTerminalFilterByProtoTyped(const envoy::extensions::filters::network::geoip::v3::Geoip&,
                                    Server::Configuration::ServerFactoryContext&) override {
    return false;
  }
};

} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
