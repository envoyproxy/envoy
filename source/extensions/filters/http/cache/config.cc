#include <memory>

#include "envoy/common/time.h"
#include "envoy/config/filter/http/cache/v2alpha/cache.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/stats/scope.h"

#include "extensions/filters/http/cache/cache_filter.h"
#include "extensions/filters/http/cache/config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

Http::FilterFactoryCb CacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::cache::v2alpha::Cache& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  return [config, stats_prefix, &context](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        CacheFilter::make(config, stats_prefix, context.scope(), context.timeSource()));
  };
}

REGISTER_FACTORY(CacheFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
