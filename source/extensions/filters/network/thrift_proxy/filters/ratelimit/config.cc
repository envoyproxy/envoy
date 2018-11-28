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
  Filters::Common::RateLimit::ClientPtr ratelimit_client =
      Filters::Common::RateLimit::rateLimitClient(
          context, PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20));
  std::shared_ptr<Filter> filter = std::make_shared<Filter>(config, std::move(ratelimit_client));
  // This lambda captures the shared_ptrs created above, thus preserving the
  // reference count. Moreover, keep in mind the capture list determines
  // destruction order.
  return [config, filter, ratelimit_client](
             ThriftProxy::ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(filter);
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
