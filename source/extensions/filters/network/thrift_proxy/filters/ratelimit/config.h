#pragma once

#include "envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3/rate_limit.pb.validate.h"

#include "source/extensions/filters/common/ratelimit/ratelimit.h"
#include "source/extensions/filters/network/thrift_proxy/filters/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace RateLimitFilter {

using namespace Envoy::Extensions::NetworkFilters;

/**
 * Config registration for the rate limit filter. @see NamedThriftFilterConfigFactory.
 */
class RateLimitFilterConfig
    : public ThriftProxy::ThriftFilters::FactoryBase<
          envoy::extensions::filters::network::thrift_proxy::filters::ratelimit::v3::RateLimit> {
public:
  RateLimitFilterConfig() : FactoryBase("envoy.filters.thrift.rate_limit") {}

private:
  ThriftProxy::ThriftFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::thrift_proxy::filters::ratelimit::v3::RateLimit&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
