// MainCommonTest works fine in coverage tests, but it appears to break SignalsTest when
// run in the same process. It appears that MainCommon doesn't completely clean up after
// itself, possibly due to a bug in SignalAction. So for now, we can test MainCommon
// but can't measure its test coverage.
//
// TODO(issues/2580): Fix coverage tests when MainCommonTest is enabled.
// TODO(issues/2649): This test needs to be parameterized on IP versions.
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

TEST(MainCommon, ConstructDestructHotRestartEnabled) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  std::string config_file = Envoy::TestEnvironment::getCheckedEnvVar("TEST_RUNDIR") +
                            "/test/config/integration/google_com_proxy_port_0.v2.yaml";
  const char* argv[] = {"envoy-static", "-c", config_file.c_str(), "--base-id", "1", nullptr};
  MainCommon main_common(ARRAY_SIZE(argv) - 1, const_cast<char**>(argv));
}

TEST(MainCommon, ConstructDestructHotRestartDisabled) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  std::string config_file = Envoy::TestEnvironment::getCheckedEnvVar("TEST_RUNDIR") +
                            "/test/config/integration/google_com_proxy_port_0.v2.yaml";
  const char* argv[] = {"envoy-static",          "-c",   config_file.c_str(), "--base-id", "2",
                        "--disable-hot-restart", nullptr};
  MainCommon main_common(ARRAY_SIZE(argv) - 1, const_cast<char**>(argv));
}

TEST(MainCommon, LegacyMain) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  // Testing the legacy path is difficult because if we give it a valid config, it will
  // never exit. So just give it an empty config and let it fail.
  int argc = 1;
  const char* argv[] = {"envoy_static", nullptr};

#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  Envoy::SignalAction handle_sigs;
#endif

  std::unique_ptr<Envoy::OptionsImpl> options;
  int return_code = -1;
  try {
    options = std::make_unique<Envoy::OptionsImpl>(
        argc, const_cast<char**>(argv), &MainCommon::hotRestartVersion, spdlog::level::info);
  } catch (const Envoy::NoServingException& e) {
    return_code = EXIT_SUCCESS;
  } catch (const Envoy::MalformedArgvException& e) {
    return_code = EXIT_FAILURE;
  }
  if (return_code == -1) {
    return_code = Envoy::main_common(*options);
  }
  EXPECT_EQ(EXIT_FAILURE, return_code);
}

} // namespace Envoy

#endif // ENVOY_CONFIG_COVERAGE
