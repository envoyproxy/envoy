#include "source/extensions/filters/network/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "source/extensions/filters/network/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

Network::FilterFactoryCb RateLimitConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::ratelimit::v3::RateLimit& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(!proto_config.domain().empty());
  ASSERT(proto_config.descriptors_size() > 0);

  ConfigSharedPtr filter_config(
      new Config(proto_config, context.scope(), context.serverFactoryContext().runtime()));
  const std::chrono::milliseconds timeout =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20));

  THROW_IF_NOT_OK(Envoy::Config::Utility::checkTransportVersion(proto_config.rate_limit_service()));
  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
      Grpc::GrpcServiceConfigWithHashKey(proto_config.rate_limit_service().grpc_service());
  return [config_with_hash_key, &context, timeout,
          filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(
        filter_config,
        Filters::Common::RateLimit::rateLimitClient(context, config_with_hash_key, timeout)));
  };
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
LEGACY_REGISTER_FACTORY(RateLimitConfigFactory,
                        Server::Configuration::NamedNetworkFilterConfigFactory, "envoy.ratelimit");

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
