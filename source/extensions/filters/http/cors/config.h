#pragma once

#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {

/**
 * Config registration for the cors filter. @see NamedHttpFilterConfigFactory.
 */
class CorsFilterFactory : public Common::EmptyHttpFilterConfig {
public:
  CorsFilterFactory() : Common::EmptyHttpFilterConfig(HttpFilterNames::get().Cors) {}

  Http::FilterFactoryCb createFilter(const std::string& stats_prefix,
                                     Server::Configuration::FactoryContext& context) override;
};

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
