#include "source/extensions/filters/http/rate_limit_quota/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

// TODO(tyxia) Add back the parameter
Http::FilterFactoryCb RateLimitQuotaFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig&
        filter_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  FilterConfigConstSharedPtr config = std::make_shared<
      envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig>(
      filter_config);

  // TODO(tyxia) Create the rate limit client early but start the grpc stream on the first request.
  // TODO(tyxia) The filter config is shared pointer but the client is unique pointer.
  return
      [config = std::move(config), &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(std::make_shared<RateLimitQuotaFilter>(
            config, context, createRateLimitClient(context, config->rlqs_server())));
      };
}

Router::RouteSpecificFilterConfigConstSharedPtr
RateLimitQuotaFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaOverride&,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  // TODO(tyxia) Not implemented.
  return nullptr;
}

/*
For matcher
https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/common/router/config_impl.cc;rcl=460349638;l=1594

RouteActionContext context{*this, optional_http_filters, factory_context};
RouteActionValidationVisitor validation_visitor;
Matcher::MatchTreeFactory<Http::HttpMatchingData, RouteActionContext> factory(
    context, factory_context, validation_visitor);

matcher_ = factory.create(virtual_host.matcher())();
*/

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RateLimitQuotaFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
