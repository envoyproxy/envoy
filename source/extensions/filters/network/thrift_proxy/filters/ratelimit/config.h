#pragma once

#include "envoy/config/filter/thrift/rate_limit/v2alpha1/rate_limit.pb.h"
#include "envoy/config/filter/thrift/rate_limit/v2alpha1/rate_limit.pb.validate.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"
#include "extensions/filters/network/thrift_proxy/filters/factory_base.h"
#include "extensions/filters/network/thrift_proxy/filters/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace RateLimitFilter {

using namespace Envoy::Extensions::NetworkFilters;

/**
 * Config registration for the rate limit filter. @see NamedThriftFilterConfigFactory.
 */
class RateLimitFilterConfig : public ThriftProxy::ThriftFilters::FactoryBase<
                                  envoy::config::filter::thrift::rate_limit::v2alpha1::RateLimit> {
public:
  RateLimitFilterConfig()
      : FactoryBase(ThriftProxy::ThriftFilters::ThriftFilterNames::get().RATE_LIMIT) {}

private:
  ThriftProxy::ThriftFilters::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::thrift::rate_limit::v2alpha1::RateLimit& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace RateLimitFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
