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
class CorsFilterConfig : public Common::EmptyHttpFilterConfig {
public:
  Server::Configuration::HttpFilterFactoryCb
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override;

  std::string name() override { return HttpFilterNames::get().CORS; }
};

} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
