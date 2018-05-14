#pragma once

#include "extensions/filters/http/common/empty_http_filter_config.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicRouter {

/**
 * Config registration for the dynamic filter. @see NamedHttpFilterConfigFactory.
 */
class DynamicRouterFilterConfig : public Common::EmptyHttpFilterConfig {
public:

  DynamicRouterFilterConfig() : Common::EmptyHttpFilterConfig(HttpFilterNames::get().DYNAMIC_ROUTER) {}

private:
  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext& context) override;
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
