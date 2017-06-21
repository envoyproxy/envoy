#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the grpc transcoding filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcTranscodingFilterConfig : public NamedHttpFilterConfigFactory {
 public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object&,
                                          const std::string&,
                                          FactoryContext& context) override;
  std::string name() override { return "grpc_transcoding"; };
  HttpFilterType type() override { return HttpFilterType::Both; }
};

} // Configuration
} // Server
} // Envoy
