#pragma once

#include "extensions/filters/http/common/empty_http_filter_config.h"
#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicCluster {

/**
 * Config registration for the dynamic cluster filter. @see NamedHttpFilterConfigFactory.
 */
class DynamicClusterFilterConfig : public Common::EmptyHttpFilterConfig {
public:
  DynamicClusterFilterConfig()
      : Common::EmptyHttpFilterConfig(HttpFilterNames::get().DYNAMIC_CLUSTER) {}

private:
  Http::FilterFactoryCb createFilter(const std::string&,
                                     Server::Configuration::FactoryContext& context) override;
};

} // namespace DynamicCluster
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
