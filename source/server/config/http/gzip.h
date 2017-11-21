#pragma once

#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the gzip filter. @see NamedHttpFilterConfigFactory.
 * TODO(gsagula): convert to envoy::api::v2::filter
 */
class GzipFilterConfig : public NamedHttpFilterConfigFactory {
public:
  HttpFilterFactoryCb createFilterFactory(const Json::Object& json_config,
                                          const std::string& stat_prefix,
                                          FactoryContext& context) override;

  std::string name() override { return Config::HttpFilterNames::get().GZIP; }
};

} // namespace Configuration
} // namespace Server
} // namespace Envoy
