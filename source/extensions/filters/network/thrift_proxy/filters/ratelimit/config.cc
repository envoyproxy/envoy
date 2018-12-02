#include "extensions/filters/network/thrift_proxy/filters/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/thrift/rate_limit/v2alpha1/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "extensions/filters/common/ratelimit/ratelimit_registration.h"
#include "extensions/filters/network/thrift_proxy/filters/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace RateLimitFilter {

using namespace Envoy::Extensions::NetworkFilters;

ThriftProxy::ThriftFilters::FilterFactoryCb
RateLimitFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::thrift::rate_limit::v2alpha1::RateLimit& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.domain().empty());
  ConfigSharedPtr config(new Config(proto_config, context.localInfo(), context.scope(),
                                    context.runtime(), context.clusterManager()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20);
  Filters::Common::RateLimit::ClientFactoryPtr client_factory =
      Filters::Common::RateLimit::rateLimitClientFactory(context);
  return [client_factory, timeout_ms,
          config](ThriftProxy::ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    // When we introduce rate limit service config in filters, we should validate here that it
    // matches with bootstrap.
    callbacks.addDecoderFilter(std::make_shared<Filter>(
        config, client_factory->create(std::chrono::milliseconds(timeout_ms))));
  };
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RateLimitFilterConfig,
                                 ThriftProxy::ThriftFilters::NamedThriftFilterConfigFactory>
    register_;

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
