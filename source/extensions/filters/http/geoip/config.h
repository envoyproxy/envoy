#pragma once

#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.h"
#include "envoy/extensions/filters/http/geoip/v3/geoip.pb.validate.h"
#include "envoy/geoip/geoip_provider_driver.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

/**
 * Config registration for the geoip filter. @see NamedHttpFilterConfigFactory.
 */
class GeoipFilterFactory
    : public Common::ExceptionFreeFactoryBase<envoy::extensions::filters::http::geoip::v3::Geoip> {
public:
  GeoipFilterFactory() : ExceptionFreeFactoryBase("envoy.filters.http.geoip") {}

  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::geoip::v3::Geoip& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::geoip::v3::Geoip& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

private:
  // Shared factory creation used by both the downstream (FactoryContext) and route/vhost-level
  // (ServerFactoryContext) paths. A GenericFactoryContext is used so the filter's scope and
  // validation visitor stay correct for each path, while the provider driver is created with the
  // server factory context.
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactory(const envoy::extensions::filters::http::geoip::v3::Geoip& proto_config,
                      const std::string& stat_prefix,
                      Server::Configuration::GenericFactoryContext& context);
};

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
