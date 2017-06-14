#pragma once

#include <string>

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the grpc HTTP1 bridge filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcHttp1BridgeFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object&, const std::string&,
                                          FactoryContext& context) override;
  std::string name() override { return "grpc_http1_bridge"; }
  HttpFilterType type() override { return HttpFilterType::Both; }
};

} // Configuration
} // Server
} // Envoy
