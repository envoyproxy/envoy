#pragma once

#include "envoy/config/filter/http/dynamic_forward_proxy/v2alpha/dynamic_forward_proxy.pb.h"
#include "envoy/config/filter/http/dynamic_forward_proxy/v2alpha/dynamic_forward_proxy.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {

/**
 * Config registration for the dynamic forward proxy filter.
 */
class DynamicForwardProxyFilterFactory
    : public Common::FactoryBase<
          envoy::config::filter::http::dynamic_forward_proxy::v2alpha::FilterConfig,
          envoy::config::filter::http::dynamic_forward_proxy::v2alpha::PerRouteConfig> {
public:
  DynamicForwardProxyFilterFactory() : FactoryBase(HttpFilterNames::get().DynamicForwardProxy) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::dynamic_forward_proxy::v2alpha::FilterConfig& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::config::filter::http::dynamic_forward_proxy::v2alpha::PerRouteConfig& config,
      Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) override;
};

DECLARE_FACTORY(DynamicForwardProxyFilterFactory);

} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
