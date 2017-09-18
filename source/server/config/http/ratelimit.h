#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the rate limit filter. @see NamedHttpFilterConfigFactory.
 */
class RateLimitFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& config, const std::string&,
                                          FactoryContext& context) override;
  std::string name() override { return Config::HttpFilterNames::get().RATE_LIMIT; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
