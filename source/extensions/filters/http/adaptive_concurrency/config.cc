#include "extensions/filters/http/adaptive_concurrency/config.h"

#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"
#include "extensions/filters/http/adaptive_concurrency/controller/gradient_controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

Http::FilterFactoryCb AdaptiveConcurrencyFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency& config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  auto acc_stats_prefix = stats_prefix + "adaptive_concurrency.";

  std::shared_ptr<Controller::ConcurrencyController> controller;
  using Proto = envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency;
  ASSERT(config.concurrency_controller_config_case() ==
         Proto::ConcurrencyControllerConfigCase::kGradientControllerConfig);
  auto gradient_controller_config =
      Controller::GradientControllerConfig(config.gradient_controller_config(), context.runtime());
  controller = std::make_shared<Controller::GradientController>(
      std::move(gradient_controller_config), context.dispatcher(), context.runtime(),
      acc_stats_prefix + "gradient_controller.", context.scope(), context.api().randomGenerator(),
      context.timeSource());

  AdaptiveConcurrencyFilterConfigSharedPtr filter_config(
      new AdaptiveConcurrencyFilterConfig(config, context.runtime(), std::move(acc_stats_prefix),
                                          context.scope(), context.timeSource()));

  return [filter_config, controller](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(
        std::make_shared<AdaptiveConcurrencyFilter>(filter_config, controller));
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
