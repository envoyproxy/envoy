#pragma once

#include <string>

#include "envoy/server/filter_config.h"

// #include "envoy/server/instance.h"

// #include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the ExtAuth filter. @see HttpFilterConfigFactory.
 */
class ExtAuthConfig : public NamedHttpFilterConfigFactory {
public:
  std::string name() override { return "extauth"; }

  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stats_prefix,
                                          FactoryContext& context) override;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
