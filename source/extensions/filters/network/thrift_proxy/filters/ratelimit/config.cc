#include "source/extensions/filters/network/thrift_proxy/filters/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "source/extensions/filters/network/thrift_proxy/filters/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace RateLimitFilter {

using namespace Envoy::Extensions::NetworkFilters;

ThriftProxy::ThriftFilters::FilterFactoryCb
RateLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::thrift_proxy::filters::ratelimit::v3::RateLimit&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.domain().empty());
  ConfigSharedPtr config(new Config(proto_config, context.localInfo(), context.scope(),
                                    context.runtime(), context.clusterManager()));
  const std::chrono::milliseconds timeout =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20));

  return [proto_config, &context, timeout, config,
          transport_version = Envoy::Config::Utility::getAndCheckTransportVersion(
              proto_config.rate_limit_service())](
             ThriftProxy::ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(std::make_shared<Filter>(
        config, Filters::Common::RateLimit::rateLimitClient(
                    context, proto_config.rate_limit_service().grpc_service(), timeout,
                    transport_version)));
  };
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RateLimitFilterConfig, ThriftProxy::ThriftFilters::NamedThriftFilterConfigFactory);

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
