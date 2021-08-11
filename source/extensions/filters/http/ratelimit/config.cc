#include "source/extensions/filters/http/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "source/extensions/filters/http/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitFilter {

Http::FilterFactoryCb RateLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::ratelimit::v3::RateLimit& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.domain().empty());
  FilterConfigSharedPtr filter_config(new FilterConfig(proto_config, context.localInfo(),
                                                       context.scope(), context.runtime(),
                                                       context.httpContext()));
  const std::chrono::milliseconds timeout =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20));

  return [proto_config, &context, timeout,
          transport_version =
              Config::Utility::getAndCheckTransportVersion(proto_config.rate_limit_service()),
          filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(
        filter_config, Filters::Common::RateLimit::rateLimitClient(
                           context, proto_config.rate_limit_service().grpc_service(), timeout,
                           transport_version)));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
RateLimitFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::ratelimit::v3::RateLimitPerRoute& proto_config,
    Server::Configuration::ServerFactoryContext&, ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<FilterConfigPerRoute>(proto_config);
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RateLimitFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.rate_limit"};

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
