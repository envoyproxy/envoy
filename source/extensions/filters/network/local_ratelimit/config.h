#pragma once

#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

/**
 * Config registration for the local rate limit filter. @see NamedNetworkFilterConfigFactory.
 */
class LocalRateLimitConfigFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit> {
public:
  LocalRateLimitConfigFactory() : FactoryBase(NetworkFilterNames::get().LocalRateLimit) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
