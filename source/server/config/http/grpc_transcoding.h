#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the grpc HTTP1 bridge filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcTranscodingFilterConfig : public NamedHttpFilterConfigFactory {
 public:
  HttpFilterFactoryCb createFilterFactory(HttpFilterType type, const Json::Object&,
                                          const std::string&, Server::Instance& server) override;
  std::string name() override;
};

} // Configuration
} // Server
} // Envoy
