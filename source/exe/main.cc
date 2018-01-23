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
 * deployment such as initializing signal handling. It calls main_common
 * after setting up command line options.
 */
int main(int argc, char** argv) {
#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  Envoy::SignalAction handle_sigs;
#endif

#ifdef ENVOY_HOT_RESTART
  // Enabled by default, except on OS X. Control with "bazel --define=hot_restart=disabled"
  const Envoy::OptionsImpl::HotRestartVersionCb hot_restart_version_cb =
      [](uint64_t max_num_stats, uint64_t max_stat_name_len) {
        return Envoy::Server::HotRestartImpl::hotRestartVersion(max_num_stats, max_stat_name_len);
      };
#else
  const Envoy::OptionsImpl::HotRestartVersionCb hot_restart_version_cb = [](uint64_t, uint64_t) {
    return "disabled";
  };
#endif

  std::unique_ptr<Envoy::OptionsImpl> options;
  try {
    options = std::make_unique<Envoy::OptionsImpl>(argc, argv, hot_restart_version_cb,
                                                   spdlog::level::info);
  } catch (const Envoy::NoServingException& e) {
    return 0;
  } catch (const Envoy::MalformedArgvException& e) {
    return 1;
  }
  return Envoy::main_common(*options);
}
