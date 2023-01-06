#include "library/common/engine_common.h"

#include "source/common/common/random_generator.h"
#include "source/common/runtime/runtime_impl.h"

namespace Envoy {

// Allows for using the new API listener when building with --copt=-DUSE_API_LISTENER
#ifdef USE_API_LISTENER 
const char* listener_type = "envoy.listener_manager_impl.api";
#else
const char* listener_type = "envoy.listener_manager_impl.default";
#endif

std::string hotRestartVersion(bool) { return "disabled"; }

EngineCommon::EngineCommon(int argc, const char* const* argv, bool create_logger)
    : options_(argc, argv, &hotRestartVersion, spdlog::level::info, listener_type),
      base_(options_, real_time_system_, default_listener_hooks_, prod_component_factory_,
            std::make_unique<PlatformImpl>(), std::make_unique<Random::RandomGeneratorImpl>(),
            nullptr, create_logger) {
  // Disabling signal handling in the options makes it so that the server's event dispatcher _does
  // not_ listen for termination signals such as SIGTERM, SIGINT, etc
  // (https://github.com/envoyproxy/envoy/blob/048f4231310fbbead0cbe03d43ffb4307fff0517/source/server/server.cc#L519).
  // Previous crashes in iOS were experienced due to early event loop exit as described in
  // https://github.com/envoyproxy/envoy-mobile/issues/831. Ignoring termination signals makes it
  // more likely that the event loop will only exit due to Engine destruction
  // https://github.com/envoyproxy/envoy-mobile/blob/a72a51e64543882ea05fba3c76178b5784d39cdc/library/common/engine.cc#L105.
  options_.setSignalHandling(false);
}

} // namespace Envoy
