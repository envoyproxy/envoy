#pragma once

#include <string>

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the cors filter. @see NamedHttpFilterConfigFactory.
 */
class CorsFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stats_prefix,
                                          FactoryContext& context) override;
  std::string name() override { return "cors"; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
