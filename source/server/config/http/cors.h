#pragma once

#include <string>

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the cors filter. @see NamedHttpFilterConfigFactory.
 */
class CorsFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object&,
                                          const std::string&,
                                          FactoryContext&) override;
  HttpFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message&,
                                                   const std::string&,
                                                   FactoryContext&) override;

  std::string name() override { return Config::HttpFilterNames::get().CORS; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
