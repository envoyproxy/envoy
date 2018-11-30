#include <iostream>
#include <memory>

#include "common/common/compiler_requirements.h"
#include "common/common/perf_annotation.h"
#include "common/event/libevent.h"
#include "common/network/utility.h"
#include "common/stats/thread_local_store.h"

#include "exe/main_common.h"

#include "server/config_validation/server.h"
#include "server/drain_manager_impl.h"
#include "server/hot_restart_nop_impl.h"
#include "server/options_impl.h"
#include "server/proto_descriptors.h"
#include "server/server.h"
#include "server/test_hooks.h"

#include "absl/strings/str_split.h"

#ifdef ENVOY_HOT_RESTART
#include "server/hot_restart_impl.h"
#endif

#include "ares.h"

namespace Envoy {

MainCommon::MainCommon(int argc, const char* const* argv)
    : options_(argc, argv, &MainCommon::hotRestartVersion, spdlog::level::info),
      base_(options_, thread_factory_) {}

std::string MainCommon::hotRestartVersion(uint64_t max_num_stats, uint64_t max_stat_name_len,
                                          bool hot_restart_enabled) {
#ifdef ENVOY_HOT_RESTART
  if (hot_restart_enabled) {
    return Server::HotRestartImpl::hotRestartVersion(max_num_stats, max_stat_name_len);
  }
#else
  UNREFERENCED_PARAMETER(hot_restart_enabled);
  UNREFERENCED_PARAMETER(max_num_stats);
  UNREFERENCED_PARAMETER(max_stat_name_len);
#endif
  return "disabled";
}

// Legacy implementation of main_common.
//
// TODO(jmarantz): Remove this when all callers are removed. At that time, MainCommonBase
// and MainCommon can be merged. The current theory is that only Google calls this.
int main_common(OptionsImpl& options) {
  try {
    Thread::ThreadFactoryImplPosix thread_factory_;
    MainCommonBase main_common(options, thread_factory_);
    return main_common.run() ? EXIT_SUCCESS : EXIT_FAILURE;
  } catch (EnvoyException& e) {
    return EXIT_FAILURE;
  }
}

} // namespace Envoy
