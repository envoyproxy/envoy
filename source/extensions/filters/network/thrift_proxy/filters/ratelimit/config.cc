#include "extensions/filters/network/thrift_proxy/filters/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/thrift/rate_limit/v2alpha1/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/common/ratelimit/ratelimit_impl.h"
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
  // TODO(ramaraochavali): Figure out how to get the registered name here and also see if this code
  // can be used across filters.

  ratelimit_service_config_ =
      context.singletonManager().getTyped<RateLimit::RateLimitServiceConfig>(
          "ratelimit_service_config_singleton_name", [] { return nullptr; });
  if (ratelimit_service_config_) {
    ratelimit_client_factory_ = std::make_unique<Filters::Common::RateLimit::GrpcFactoryImpl>(
        ratelimit_service_config_->config_, context.clusterManager().grpcAsyncClientManager(),
        context.scope());
  } else {
    ratelimit_client_factory_ = std::make_unique<Filters::Common::RateLimit::NullFactoryImpl>();
  }

  return [config, timeout_ms, &context,
          this](ThriftProxy::ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(std::make_shared<Filter>(
        config, ratelimit_client_factory_->create(std::chrono::milliseconds(timeout_ms), context)));
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
