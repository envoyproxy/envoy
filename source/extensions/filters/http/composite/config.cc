#include "source/extensions/filters/http/composite/config.h"

#include "envoy/common/exception.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/composite/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {

absl::StatusOr<Http::FilterFactoryCb> CompositeFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::composite::v3::Composite&,
    const std::string& stat_prefix, DualInfo dual_info,
    Server::Configuration::ServerFactoryContext&) {

  const auto& prefix = stat_prefix + "composite.";
  auto stats = std::make_shared<FilterStats>(
      FilterStats{ALL_COMPOSITE_FILTER_STATS(POOL_COUNTER_PREFIX(dual_info.scope, prefix))});

  return [stats, dual_info](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(*stats, callbacks.dispatcher(), dual_info.is_upstream);
    callbacks.addStreamFilter(filter);
    callbacks.addAccessLogHandler(filter);
  };
}

/**
 * Static registration for the composite filter. @see RegisterFactory.
 */
REGISTER_FACTORY(CompositeFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamCompositeFilterFactory,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
