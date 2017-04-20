#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/config/network/http_connection_manager.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the rate limit filter. @see HttpFilterConfigFactory.
 */
class RateLimitFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object& config, const std::string&,
                                             Server::Instance& server) override;
};

} // Configuration
} // Server
