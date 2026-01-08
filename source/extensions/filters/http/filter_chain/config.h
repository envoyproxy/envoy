#pragma once

#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/filter_chain/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

/**
 * Config registration for the filter chain filter. @see NamedHttpFilterConfigFactory.
 */
class FilterChainFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig,
          envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute>,
      public Logger::Loggable<Logger::Id::filter> {
public:
  FilterChainFilterFactory() : FactoryBase("envoy.filters.http.filter_chain") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor&) override;
};

DECLARE_FACTORY(FilterChainFilterFactory);

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
