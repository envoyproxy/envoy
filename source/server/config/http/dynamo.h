#pragma once

#include <string>

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for http dynamodb filter.
 */
class DynamoFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(HttpFilterType type, const Json::Object&,
                                          const std::string& stat_prefix,
                                          Server::Instance& server) override;
  std::string name() override;
};

} // Configuration
} // Server
} // Envoy
