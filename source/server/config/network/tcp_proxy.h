#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the tcp proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class TcpProxyConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // NamedNetworkFilterConfigFactory
  NetworkFilterFactoryCb createFilterFactory(const Json::Object& config,
                                             FactoryContext& context) override;
  std::string name() override { return Config::NetworkFilterNames::get().TCP_PROXY; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
