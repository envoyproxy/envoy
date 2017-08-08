#pragma once

#include "envoy/server/filter_config.h"

namespace Envoy {
namespace Server {
namespace Configuration {

class GrpcWebFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object&, const std::string&,
                                          FactoryContext&) override;
  std::string name() override { return "grpc_web"; }
  HttpFilterType type() override { return HttpFilterType::Both; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
