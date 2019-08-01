#include "extensions/filters/http/adaptive_concurrency/config.h"

#include "envoy/config/filter/http/adaptive_concurrency/v2alpha/adaptive_concurrency.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"

#include "extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"
#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/noop_controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

Http::FilterFactoryCb AdaptiveConcurrencyFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::adaptive_concurrency::v2alpha::AdaptiveConcurrency& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  // TODO (tonya11en): Noop controller needs to be replaced with an actual
  // implementation in a future patch.
  auto noop_ctl = std::make_shared<ConcurrencyController::NoopController>();

  AdaptiveConcurrencyFilterConfigSharedPtr filter_config(new AdaptiveConcurrencyFilterConfig(
      config, context.runtime(), stats_prefix, context.scope(), context.timeSource()));

  return [filter_config, noop_ctl](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<AdaptiveConcurrencyFilter>(filter_config, noop_ctl));
  };
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
