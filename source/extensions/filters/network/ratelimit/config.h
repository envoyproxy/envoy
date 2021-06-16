#pragma once

#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/network/ratelimit/v3/rate_limit.pb.validate.h"

#include "source/extensions/filters/common/ratelimit/ratelimit.h"
#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

constexpr char RateLimitName[] = "envoy.filters.network.ratelimit";

/**
 * Config registration for the rate limit filter. @see NamedNetworkFilterConfigFactory.
 */
class RateLimitConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::network::ratelimit::v3::RateLimit> {
public:
  RateLimitConfigFactory() : FactoryBase(RateLimitName) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::ratelimit::v3::RateLimit& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
