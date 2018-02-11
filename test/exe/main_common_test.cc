// MainCommonTest works fine in coverage tests, but it appears to break SignalsTest when
// run in the same process. It appears that MainCommon doesn't completely clean up after
// itself, possibly due to a bug in SignalAction. So for now, we can test MainCommon
// but can't measure its test coverage.
//
// TODO(issues/2580): Fix coverage tests when MainCommonTest is enabled.
#ifndef ENVOY_CONFIG_COVERAGE

#include "exe/main_common.h"

#include "server/options_impl.h"

#include "test/test_common/environment.h"

#include "gtest/gtest.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#endif

#ifdef ENVOY_HOT_RESTART
#include "server/hot_restart_impl.h"
#endif

namespace Envoy {

TEST(MainCommon, ConstructDestruct) {
  // TODO(jmarantz): I think we may need to hack the config file to pick an unused port.
  std::string config_file = Envoy::TestEnvironment::getCheckedEnvVar("TEST_RUNDIR") +
                            "/configs/google_com_proxy_port_0.json";
  const char* argv[] = {"envoy-static", "-c", config_file.c_str(), nullptr};
  MainCommon main_common(3, argv, false);
}

TEST(MainCommon, LegacyMain) {
  // Testing the legacy path is difficult because if we give it a valid config, it will
  // never exit. So just give it an empty config and let it fail.
  int argc = 1;
  std::string envoy_static("envoy-static");
  char* argv[] = {&(envoy_static[0]), nullptr};

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
  int return_code = -1;
  try {
    options = std::make_unique<Envoy::OptionsImpl>(argc, argv, hot_restart_version_cb,
                                                   spdlog::level::info);
  } catch (const Envoy::NoServingException& e) {
    return_code = 0;
  } catch (const Envoy::MalformedArgvException& e) {
    return_code = 1;
  }
  if (return_code == -1) {
    return_code = Envoy::main_common(*options);
  }
  EXPECT_EQ(1, return_code);
}

} // namespace Envoy

#endif // ENVOY_CONFIG_COVERAGE
