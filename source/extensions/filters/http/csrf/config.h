#pragma once

#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Csrf {

/**
 * Config registration for the CSRF filter. @see NamedHttpFilterConfigFactory.
 */
class CsrfFilterFactory : public Common::EmptyHttpFilterConfig {
public:
  CsrfFilterFactory() : Common::EmptyHttpFilterConfig(HttpFilterNames::get().Csrf) {}

  Http::FilterFactoryCb createFilter(const std::string& stats_prefix,
                                     Server::Configuration::FactoryContext& context) override;
};

} // namespace Csrf
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
