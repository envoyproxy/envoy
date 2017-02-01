#pragma once

#include "envoy/server/instance.h"

#include "server/config/network/http_connection_manager.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the router filter. @see HttpFilterConfigFactory.
 */
class RouterFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object& json_config,
                                             const std::string& stat_prefix,
                                             Server::Instance& server) override;
};

} // Configuration
} // Server
