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
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit> {
public:
  BandwidthLimitFilterConfig() : ExceptionFreeFactoryBase("envoy.filters.http.bandwidth_limit") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::bandwidth_limit::v3::BandwidthLimit& proto_config,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
