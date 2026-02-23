#pragma once

#include "envoy/extensions/filters/http/a2a/v3/a2a.pb.h"
#include "envoy/extensions/filters/http/a2a/v3/a2a.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {

/**
 * Config factory for A2A filter.
 */
class A2aFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::a2a::v3::A2a,
                                 envoy::extensions::filters::http::a2a::v3::A2aOverride> {
public:
  // The string "envoy.filters.http.a2a" must match the name used in the
  // registration logic and your bootstrap configuration.
  A2aFilterConfigFactory() : FactoryBase("envoy.filters.http.a2a") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::a2a::v3::A2a& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::a2a::v3::A2aOverride& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy