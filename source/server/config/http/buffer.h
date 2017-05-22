#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the buffer filter. @see NamedHttpFilterConfigFactory.
 */
class BufferFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(HttpFilterType type, const Json::Object& json_config,
                                          const std::string& stats_prefix,
                                          Server::Instance& server) override;
  std::string name() override;
};

} // Configuration
} // Server
} // Envoy
