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
  auto& server_context = context.serverFactoryContext();

  ASSERT(!proto_config.domain().empty());
  FilterConfigSharedPtr filter_config(new FilterConfig(proto_config, server_context.localInfo(),
                                                       context.scope(), server_context.runtime(),
                                                       server_context.httpContext()));
  const std::chrono::milliseconds timeout =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20));

  THROW_IF_NOT_OK(Config::Utility::checkTransportVersion(proto_config.rate_limit_service()));
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
      Grpc::GrpcServiceConfigWithHashKey(proto_config.rate_limit_service().grpc_service());
  return [config_with_hash_key, &context, timeout,
          filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<Filter>(
        filter_config,
        Filters::Common::RateLimit::rateLimitClient(context, config_with_hash_key, timeout)));
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
LEGACY_REGISTER_FACTORY(RateLimitFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.rate_limit");

} // namespace RateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
