#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the rate limit filter. @see NamedNetworkFilterConfigFactory.
 */
class RateLimitConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                             FactoryContext& context) override;
  std::string name() override { return Config::NetworkFilterNames::get().RATE_LIMIT; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
