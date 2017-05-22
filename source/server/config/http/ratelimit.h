#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the rate limit filter. @see NamedHttpFilterConfigFactory.
 */
class RateLimitFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(HttpFilterType type, const Json::Object& config,
                                          const std::string&, Server::Instance& server) override;
  std::string name() override;
};

} // Configuration
} // Server
} // Envoy
