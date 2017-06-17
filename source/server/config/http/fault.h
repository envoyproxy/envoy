#pragma once

#include <string>

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the fault injection filter. @see NamedHttpFilterConfigFactory.
 */
class FaultFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stats_prefix,
                                          FactoryContext& context) override;
  std::string name() override { return "fault"; }
  HttpFilterType type() override { return HttpFilterType::Decoder; }
};

} // Configuration
} // Server
} // Envoy
