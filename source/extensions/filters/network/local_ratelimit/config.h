#pragma once

#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.validate.h"

#include "source/extensions/filters/network/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

constexpr char LocalRateLimitName[] = "envoy.filters.network.local_ratelimit";

/**
 * Config registration for the local rate limit filter. @see NamedNetworkFilterConfigFactory.
 */
class LocalRateLimitConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit> {
public:
  LocalRateLimitConfigFactory() : FactoryBase(LocalRateLimitName) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
