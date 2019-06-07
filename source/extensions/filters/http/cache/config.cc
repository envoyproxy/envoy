#include "extensions/filters/http/cache/config.h"

#include <memory>

#include "envoy/common/time.h"
#include "envoy/config/filter/http/cache/v2alpha/cache.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/stats/scope.h"

#include "extensions/filters/http/cache/cache_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

using Http::FilterChainFactoryCallbacks;
using Server::Configuration::FactoryContext;
using Server::Configuration::NamedHttpFilterConfigFactory;
using std::make_shared;
using std::string;

Http::FilterFactoryCb CacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::cache::v2alpha::Cache& config, const string& stats_prefix,
    FactoryContext& context) {
  return [config, stats_prefix, &context](FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        make_shared<CacheFilter>(config, stats_prefix, context.scope(), context.timeSource()));
  };
}

REGISTER_FACTORY(CacheFilterFactory, NamedHttpFilterConfigFactory);

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
