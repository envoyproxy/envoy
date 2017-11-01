#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

#include "api/filter/http/router.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the router filter. @see NamedHttpFilterConfigFactory.
 */
class RouterFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stat_prefix,
                                          FactoryContext& context) override;
  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message& config,
                                                   const std::string& stat_prefix,
                                                   FactoryContext& context) override;
  std::string name() override { return Config::HttpFilterNames::get().ROUTER; }

private:
  HttpFilterFactoryCb createRouterFilter(bool dynamic_stats, const std::string& stat_prefix,
                                         FactoryContext& context);
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
