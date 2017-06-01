#include <iostream>
#include <memory>

#include "exe/hot_restart.h"
#include "exe/main_common.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#endif

#include "server/options_impl.h"

#include "spdlog/spdlog.h"

// NOLINT(namespace-envoy)

/**
 * Basic Site-Specific main()
 *
 * This should be used to do setup tasks specific to a particular site's
 * deployment such as initializing signal handling.  It calls main_common
 * after setting up command line options and the hot restarter.
 */
int main(int argc, char** argv) {
#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  Envoy::SignalAction handle_sigs;
#endif

  Envoy::OptionsImpl options(argc, argv, Envoy::Server::SharedMemory::version(),
                             spdlog::level::warn);

  std::unique_ptr<Envoy::Server::HotRestartImpl> restarter;
  try {
    restarter.reset(new Envoy::Server::HotRestartImpl(options));
  } catch (Envoy::EnvoyException& e) {
    std::cerr << "unable to initialize hot restart: " << e.what() << std::endl;
    return 1;
  }

  return Envoy::main_common(options, *restarter);
}
