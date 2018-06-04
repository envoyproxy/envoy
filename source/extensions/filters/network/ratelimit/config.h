#pragma once

#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.h"
#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

/**
 * Config registration for the rate limit filter. @see NamedNetworkFilterConfigFactory.
 */
class RateLimitConfigFactory
    : public Common::FactoryBase<envoy::config::filter::network::rate_limit::v2::RateLimit> {
public:
  RateLimitConfigFactory() : FactoryBase(NetworkFilterNames::get().RATE_LIMIT) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::rate_limit::v2::RateLimit& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
