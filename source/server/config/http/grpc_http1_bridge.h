#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "server/config/http/empty_http_filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the grpc HTTP1 bridge filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcHttp1BridgeFilterConfig : public EmptyHttpFilterConfig {
public:
  HttpFilterFactoryCb createFilter(const std::string&, FactoryContext& context) override;

  std::string name() override { return Config::HttpFilterNames::get().GRPC_HTTP1_BRIDGE; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
