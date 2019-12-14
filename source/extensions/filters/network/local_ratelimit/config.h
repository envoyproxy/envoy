#pragma once

#include "envoy/config/filter/network/local_rate_limit/v2alpha/local_rate_limit.pb.h"
#include "envoy/config/filter/network/local_rate_limit/v2alpha/local_rate_limit.pb.validate.h"

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
          envoy::config::filter::network::local_rate_limit::v2alpha::LocalRateLimit> {
public:
  LocalRateLimitConfigFactory() : FactoryBase(NetworkFilterNames::get().LocalRateLimit) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::local_rate_limit::v2alpha::LocalRateLimit& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
