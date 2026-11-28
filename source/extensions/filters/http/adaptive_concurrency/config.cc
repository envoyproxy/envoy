#include "source/extensions/filters/http/adaptive_concurrency/config.h"

#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.h"
#include "envoy/extensions/filters/http/adaptive_concurrency/v3/adaptive_concurrency.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"
#include "source/extensions/filters/http/adaptive_concurrency/controller/ewma_controller.h"
#include "source/extensions/filters/http/adaptive_concurrency/controller/gradient_controller.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

absl::StatusOr<Http::FilterFactoryCb> AdaptiveConcurrencyFilterFactory::createFilterFactory(
    const envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency& config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& server_context,
    Stats::Scope& scope) {

  auto acc_stats_prefix = stats_prefix + "adaptive_concurrency.";

  std::shared_ptr<Controller::ConcurrencyController> controller;
  using Proto = envoy::extensions::filters::http::adaptive_concurrency::v3::AdaptiveConcurrency;
  switch (config.concurrency_controller_config_case()) {
  case Proto::kGradientControllerConfig: {
    absl::Status creation_status = absl::OkStatus();
    auto gradient_controller_config = Controller::GradientControllerConfig(
        config.gradient_controller_config(), server_context.runtime(), creation_status);
    RETURN_IF_NOT_OK_REF(creation_status);
    controller = std::make_shared<Controller::GradientController>(
        std::move(gradient_controller_config), server_context.mainThreadDispatcher(),
        server_context.runtime(), acc_stats_prefix + "gradient_controller.", scope,
        server_context.api().randomGenerator(), server_context.timeSource());
    break;
  }
  case Proto::kEwmaControllerConfig: {
    auto result = Controller::EwmaController::create(
        config.ewma_controller_config(), server_context.mainThreadDispatcher(),
        acc_stats_prefix + "ewma_controller.", scope, server_context.timeSource());
    RETURN_IF_NOT_OK_REF(result.status());
    controller = std::move(result.value());
    break;
  }
  case Proto::CONCURRENCY_CONTROLLER_CONFIG_NOT_SET:
    return absl::InvalidArgumentError("adaptive_concurrency: controller configuration is required");
  }

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
