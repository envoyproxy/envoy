#include "library/common/envoy_mobile_main_common.h"

#include "common/common/random_generator.h"
#include "common/runtime/runtime_impl.h"

namespace Envoy {

MobileMainCommon::MobileMainCommon(int argc, const char* const* argv)
    : options_(argc, argv, &MainCommon::hotRestartVersion, spdlog::level::info),
      base_(options_, real_time_system_, default_listener_hooks_, prod_component_factory_,
            std::make_unique<Random::RandomGeneratorImpl>(), platform_impl_.threadFactory(),
            platform_impl_.fileSystem(), nullptr) {
  // Disabling signal handling in the options makes it so that the server's event dispatcher _does
  // not_ listen for termination signals such as SIGTERM, SIGINT, etc
  // (https://github.com/envoyproxy/envoy/blob/048f4231310fbbead0cbe03d43ffb4307fff0517/source/server/server.cc#L519).
  // Previous crashes in iOS were experienced due to early event loop exit as described in
  // https://github.com/lyft/envoy-mobile/issues/831. Ignoring termination signals makes it more
  // likely that the event loop will only exit due to Engine destruction
  // https://github.com/lyft/envoy-mobile/blob/a72a51e64543882ea05fba3c76178b5784d39cdc/library/common/engine.cc#L105.
  options_.setSignalHandling(false);
}

} // namespace Envoy
