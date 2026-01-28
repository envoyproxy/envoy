#include "source/extensions/filters/http/adaptive_concurrency/config.h"

#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"
#include "source/extensions/filters/http/adaptive_concurrency/controller/gradient_controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

Http::FilterFactoryCb AdaptiveConcurrencyFilterFactory::createFilterFactory(
    const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency& config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& server_context,
    Stats::Scope& scope) {

  auto acc_stats_prefix = stats_prefix + "adaptive_concurrency.";

  std::shared_ptr<Controller::ConcurrencyController> controller;
  using Proto = envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency;
  ASSERT(config.concurrency_controller_config_case() ==
         Proto::ConcurrencyControllerConfigCase::kGradientControllerConfig);
  auto gradient_controller_config = Controller::GradientControllerConfig(
      config.gradient_controller_config(), server_context.runtime());
  controller = std::make_shared<Controller::GradientController>(
      std::move(gradient_controller_config), server_context.mainThreadDispatcher(),
      server_context.runtime(), acc_stats_prefix + "gradient_controller.", scope,
      server_context.api().randomGenerator(), server_context.timeSource());

  AdaptiveConcurrencyFilterConfigSharedPtr filter_config(new AdaptiveConcurrencyFilterConfig(
      config, server_context.runtime(), std::move(acc_stats_prefix), scope,
      server_context.timeSource()));

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
