#include <iostream>
#include <memory>

#include "exe/main_common.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#endif

#ifdef ENVOY_HOT_RESTART
#include "server/hot_restart_impl.h"
#endif

#include "server/options_impl.h"

#include "spdlog/spdlog.h"

// NOLINT(namespace-envoy)

/**
 * Basic Site-Specific main()
 *
 * This should be used to do setup tasks specific to a particular site's
 * deployment such as initializing signal handling.  It calls main_common
 * after setting up command line options.
 */
int main(int argc, char** argv) {
#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  Envoy::SignalAction handle_sigs;
#endif

#ifdef ENVOY_HOT_RESTART
  // Enabled by default, except on OS X. Control with "bazel --define=hot_restart=disabled"
  const std::string shared_mem_version = Envoy::Server::SharedMemory::version();
#else
  const std::string shared_mem_version = "disabled";
#endif

  Envoy::OptionsImpl options(argc, argv, shared_mem_version, spdlog::level::warn);

  return Envoy::main_common(options);
}
