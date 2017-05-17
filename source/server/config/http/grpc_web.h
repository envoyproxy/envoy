#pragma once

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class GrpcWebFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object&, const std::string&,
                                             Server::Instance& server) override;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
