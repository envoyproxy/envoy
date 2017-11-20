#pragma once

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class GrpcWebFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object&, const std::string&,
                                          FactoryContext&) override;
  std::string name() override { return Config::HttpFilterNames::get().GRPC_WEB; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
