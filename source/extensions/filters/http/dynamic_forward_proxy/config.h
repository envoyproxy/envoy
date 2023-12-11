#pragma once

#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

/**
 * Config registration for the dynamic forward proxy filter.
 */
class DynamicForwardProxyFilterFactory
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig,
          envoy::extensions::filters::http::dynamic_forward_proxy::v3::PerRouteConfig> {
public:
  DynamicForwardProxyFilterFactory()
      : ExceptionFreeFactoryBase("envoy.filters.http.dynamic_forward_proxy") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::dynamic_forward_proxy::v3::PerRouteConfig& config,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

DECLARE_FACTORY(DynamicForwardProxyFilterFactory);

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
