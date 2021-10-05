#pragma once

#include "envoy/extensions/filters/http/bandwidth_limit/v3/bandwidth_limit.pb.h"
#include "envoy/extensions/filters/http/bandwidth_limit/v3/bandwidth_limit.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

/**
 * Config registration for the bandwidth limit filter. @see NamedHttpFilterConfigFactory.
 */
class BandwidthLimitFilterConfig
    : public Common::FactoryBase<
          envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit> {
public:
  BandwidthLimitFilterConfig() : FactoryBase("envoy.filters.http.bandwidth_limit") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
