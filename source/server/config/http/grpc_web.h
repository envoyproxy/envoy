#pragma once

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class GrpcWebFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(HttpFilterType type, const Json::Object&,
                                          const std::string&, Server::Instance&) override;

  std::string name() override;
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
