#pragma once

#include <string>

#include "envoy/server/instance.h"

#include "common/config/well_known_names.h"

#include "server/config/network/http_connection_manager.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the gRPC JSON transcoder filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcJsonTranscoderFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object&, const std::string&,
                                          FactoryContext& context) override;
  std::string name() override { return Config::HttpFilterNames::get().GRPC_JSON_TRANSCODER; };
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
