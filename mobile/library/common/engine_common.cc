#include "library/common/engine_common.h"

#include "source/common/common/random_generator.h"
#include "source/common/runtime/runtime_impl.h"

namespace Envoy {
namespace {

bool useApiListener(const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
#ifndef ENVOY_MOBILE_ENABLE_LISTENER
  return true;
#endif
  if (bootstrap.layered_runtime().layers().size() == 0) {
    return false;
  }
  for (auto field : bootstrap.layered_runtime().layers(0).static_layer().fields()) {
    if (absl::string_view(field.first) == "envoy") {
      for (auto inner_field : field.second.struct_value().fields()) {
        if (absl::string_view(inner_field.first) == "reloadable_features") {
          for (auto flag_field : inner_field.second.struct_value().fields()) {
            if (absl::string_view(flag_field.first) == "use_api_listener") {
              return flag_field.second.bool_value();
            }
          }
        }
      }
    }
  }
  return false;
}

} // namespace

EngineCommon::EngineCommon(std::unique_ptr<Envoy::OptionsImpl>&& options)
    : options_(std::move(options)) {
  // TODO(alyssar) when this defaults true, move E-M default config over to boostrap config.
  if (useApiListener(options_->configProto()) ||
      absl::StrContains(options_->configYaml(), "use_api_listener: true")) {
    options_->setListenerManager("envoy.listener_manager_impl.api");
  }

  base_ = std::make_unique<StrippedMainBase>(
      *options_, real_time_system_, default_listener_hooks_, prod_component_factory_,
      std::make_unique<PlatformImpl>(), std::make_unique<Random::RandomGeneratorImpl>(), nullptr);

  // Disabling signal handling in the options makes it so that the server's event dispatcher _does
  // not_ listen for termination signals such as SIGTERM, SIGINT, etc
  // (https://github.com/envoyproxy/envoy/blob/048f4231310fbbead0cbe03d43ffb4307fff0517/source/server/server.cc#L519).
  // Previous crashes in iOS were experienced due to early event loop exit as described in
  // https://github.com/envoyproxy/envoy-mobile/issues/831. Ignoring termination signals makes it
  // more likely that the event loop will only exit due to Engine destruction
  // https://github.com/envoyproxy/envoy-mobile/blob/a72a51e64543882ea05fba3c76178b5784d39cdc/library/common/engine.cc#L105.
  options_->setSignalHandling(false);
}

} // namespace Envoy
