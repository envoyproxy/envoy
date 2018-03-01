// MainCommonTest works fine in coverage tests, but it appears to break SignalsTest when
// run in the same process. It appears that MainCommon doesn't completely clean up after
// itself, possibly due to a bug in SignalAction. So for now, we can test MainCommon
// but can't measure its test coverage.
//
// TODO(issues/2580): Fix coverage tests when MainCommonTest is enabled.
// TODO(issues/2649): This test needs to be parameterized on IP versions.
#ifndef ENVOY_CONFIG_COVERAGE

#include "common/runtime/runtime_impl.h"

#include "exe/main_common.h"

#include "server/options_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#endif

namespace Envoy {

class MainCommonTest : public testing::Test {
public:
  MainCommonTest()
      : config_file_(Envoy::TestEnvironment::getCheckedEnvVar("TEST_RUNDIR") +
                     "/test/config/integration/google_com_proxy_port_0.v2.yaml"),
        random_string_(fmt::format("{}", random_generator_.random() % 1024)),
        argv_({"envoy-static", "--base-id", random_string_.c_str(), nullptr}) {}

  char** argv() { return const_cast<char**>(&argv_[0]); }
  int argc() { return argv_.size() - 1; }

  void addConfig() {
    addArg("-c");
    addArg(config_file_.c_str());
  }

  // Adds an argument, assuring that argv remains null-terminated.
  void addArg(const char* arg) {
    argv_[argv_.size() - 1] = arg;
    argv_.push_back(nullptr);
  }

  std::string config_file_;
  Runtime::RandomGeneratorImpl random_generator_;
  std::string random_string_;
  std::vector<const char*> argv_;
};

TEST_F(MainCommonTest, ConstructDestructHotRestartEnabled) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  addConfig();
  VERBOSE_EXPECT_NO_THROW(MainCommon main_common(argc(), argv()));
}

TEST_F(MainCommonTest, ConstructDestructHotRestartDisabled) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  addConfig();
  addArg("--disable-hot-restart");
  VERBOSE_EXPECT_NO_THROW(MainCommon main_common(argc(), argv()));
}

TEST_F(MainCommonTest, LegacyMain) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }

#ifdef ENVOY_HANDLE_SIGNALS
  // Enabled by default. Control with "bazel --define=signal_trace=disabled"
  Envoy::SignalAction handle_sigs;
#endif

  std::unique_ptr<Envoy::OptionsImpl> options;
  int return_code = -1;
  try {
    options = std::make_unique<Envoy::OptionsImpl>(argc(), argv(), &MainCommon::hotRestartVersion,
                                                   spdlog::level::info);
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
