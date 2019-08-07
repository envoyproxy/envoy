#include "extensions/filters/http/adaptive_concurrency/config.h"

#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

Http::FilterFactoryCb AdaptiveConcurrencyFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::adaptive_concurrency::v2alpha::AdaptiveConcurrency&,
    const std::string&, Server::Configuration::FactoryContext&) {

  return [](Http::FilterChainFactoryCallbacks&) -> void {};
}

/**
 * Static registration for the adaptive_concurrency filter. @see RegisterFactory.
 */
REGISTER_FACTORY(AdaptiveConcurrencyFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
