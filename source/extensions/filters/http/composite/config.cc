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
    const std::string& stat_prefix, DualInfo,
    Server::Configuration::FactoryContext& factory_context) {
  return createFilterFactory(stat_prefix, factory_context);
}

absl::StatusOr<Http::FilterFactoryCb> CompositeFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::composite::v3::Composite&,
    const std::string& stat_prefix, DualInfo,
    Server::Configuration::UpstreamFactoryContext& factory_context) {
  return createFilterFactory(stat_prefix, factory_context);
}

template <class FactoryCtx>
absl::StatusOr<Http::FilterFactoryCb>
CompositeFilterFactory::createFilterFactory(const std::string& stat_prefix,
                                            FactoryCtx& factory_context) {

  const auto& prefix = stat_prefix + "composite.";
  auto stats = std::make_shared<FilterStats>(FilterStats{
      ALL_COMPOSITE_FILTER_STATS(POOL_COUNTER_PREFIX(factory_context.scope(), prefix))});

  return [stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<Filter>(*stats, callbacks.dispatcher());
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
