// MainCommonTest works fine in coverage tests, but it appears to break SignalsTest when
// run in the same process. It appears that MainCommon doesn't completely clean up after
// itself, possibly due to a bug in SignalAction. So for now, we can test MainCommon
// but can't measure its test coverage.
//
// TODO(issues/2580): Fix coverage tests when MainCommonTest is enabled.
// TODO(issues/2649): This test needs to be parameterized on IP versions.
#ifndef ENVOY_CONFIG_COVERAGE

#include <unistd.h>

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/runtime/runtime_impl.h"

#include "exe/main_common.h"

#include "server/options_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "exe/signal_action.h"
#endif

using testing::HasSubstr;

namespace Envoy {

/**
 * Captures common functions needed for invoking MainCommon. Generates a
 * unique --base-id setting based on the pid and a random number. Maintains
 * an argv array that is terminated with nullptr. Identifies the config
 * file relative to $TEST_RUNDIR.
 *
 * TODO(jmarantz): Make these tests work with ipv6. See
 * https://github.com/envoyproxy/envoy/issues/2649
 */
class MainCommonTest : public testing::Test {
public:
  MainCommonTest()
      : config_file_(Envoy::TestEnvironment::getCheckedEnvVar("TEST_RUNDIR") +
                     "/test/config/integration/google_com_proxy_port_0.v2.yaml"),
        random_string_(fmt::format("{}", computeBaseId())),
        argv_({"envoy-static", "--base-id", random_string_.c_str(), "-c", config_file_.c_str(),
               nullptr}),
        envoy_return_(false), envoy_started_(false), envoy_finished_(false) {}

  /**
   * Computes a numeric ID to incorporate into the names of
   * shared-memory segments and domain sockets, to help keep them
   * distinct from other tests that might be running concurrently.
   *
   * The PID is needed to isolate namespaces between concurrent
   * processes in CI. The random number generator is needed
   * sequentially executed test methods fail with an error in
   * bindDomainSocket if the the same base-id is re-used.
   *
   * @return uint32_t a unique numeric ID based on the PID and a random number.
   */
  static uint32_t computeBaseId() {
    Runtime::RandomGeneratorImpl random_generator_;
    // Pick a prime number to give more of the 32-bits of entropy to the PID, and the
    // remainder to the random number.
    const uint32_t four_digit_prime = 7919;
    return getpid() * four_digit_prime + random_generator_.random() % four_digit_prime;
  }

  const char* const* argv() { return &argv_[0]; }
  int argc() { return argv_.size() - 1; }

  // Adds an argument, assuring that argv remains null-terminated.
  void addArg(const char* arg) {
    ASSERT(!argv_.empty());
    const size_t last = argv_.size() - 1;
    ASSERT(argv_[last] == nullptr); // invariant established in ctor, maintained below.
    argv_[last] = arg;              // guaranteed non-empty
    argv_.push_back(nullptr);
  }

  // Adds options to make Envoy exit immediately after initializtion.
  void initOnly() {
    addArg("--mode");
    addArg("init_only");
  }

  // Runs a an admin request specified in path, blocking until completion, and
  // returning the response body.
  std::string adminRequest(absl::string_view path, absl::string_view method) {
    Thread::MutexBasicLockable mutex;
    Thread::CondVar condvar;
    bool done = false;
    std::string out;
    main_common_->adminRequest(
        path, method,
        [&mutex, &condvar, &done, &out](const Http::HeaderMap& /*response_headers*/,
                                        absl::string_view body) {
          Thread::LockGuard lock(mutex);
          out = std::string(body);
          done = true;
          condvar.notifyOne();
        });
    {
      Thread::LockGuard lock(mutex);
      while (!done) {
        condvar.wait(mutex);
      }
    }
    return out;
  }

  // Initiates Envoy running in its own thread.
  void startEnvoy() {
    envoy_thread_ = std::make_unique<Thread::Thread>([this]() {
      // Note: main_common_ is accesesed in the testing thread, but
      // is race-free, as MainCommon::run() does not return until
      // triggered with an adminRequest POST to /quitquitquit, which
      // is done in the testing thread.
      main_common_ = std::make_unique<MainCommon>(argc(), argv());
      {
        Thread::LockGuard lock(mutex_);
        envoy_started_ = true;
        started_condvar_.notifyOne();
      }
      bool status = main_common_->run();
      main_common_.reset();
      {
        Thread::LockGuard lock(mutex_);
        envoy_finished_ = true;
        envoy_return_ = status;
        finished_condvar_.notifyOne();
      }
    });
  }

  // Having started Envoy in its own thread, this waits for Envoy to be
  // responsive by sending it a simple stats request.
  void waitForEnvoyToStart() {
    Thread::LockGuard lock(mutex_);
    while (!envoy_started_) {
      started_condvar_.wait(mutex_);
    }
  }

  // Having sent a /quitquitquit to Envoy, this blocks until Envoy exits.
  bool waitForEnvoyToExit() {
    {
      Thread::LockGuard lock(mutex_);
      while (!envoy_finished_) {
        finished_condvar_.wait(mutex_);
      }
    }
    envoy_thread_->join();
    return envoy_return_;
  }

  std::string config_file_;
  std::string random_string_;
  std::vector<const char*> argv_;
  std::unique_ptr<Thread::Thread> envoy_thread_;
  std::unique_ptr<MainCommon> main_common_;
  Thread::CondVar started_condvar_;
  Thread::CondVar finished_condvar_;
  Thread::MutexBasicLockable mutex_;
  bool envoy_return_;
  bool envoy_started_ GUARDED_BY(mutex_);
  bool envoy_finished_ GUARDED_BY(mutex_);
};

// Exercise the codepath to instantiate MainCommon and destruct it, with hot restart.
TEST_F(MainCommonTest, ConstructDestructHotRestartEnabled) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  VERBOSE_EXPECT_NO_THROW(MainCommon main_common(argc(), argv()));
}

// Exercise the codepath to instantiate MainCommon and destruct it, without hot restart.
TEST_F(MainCommonTest, ConstructDestructHotRestartDisabled) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  addArg("--disable-hot-restart");
  VERBOSE_EXPECT_NO_THROW(MainCommon main_common(argc(), argv()));
}

// Exercise init_only explicitly.
TEST_F(MainCommonTest, ConstructDestructHotRestartDisabledNoInit) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  addArg("--disable-hot-restart");
  initOnly();
  MainCommon main_common(argc(), argv());
  EXPECT_TRUE(main_common.run());
}

// Ensurees that existing users of main_common() can link.
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
    initOnly();
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
  EXPECT_EQ(EXIT_SUCCESS, return_code);
}

TEST_F(MainCommonTest, AdminRequestGetStatsAndQuit) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  addArg("--disable-hot-restart");
  startEnvoy();
  waitForEnvoyToStart();
  EXPECT_THAT(adminRequest("/stats", "GET"), HasSubstr("filesystem.reopen_failed"));
  adminRequest("/quitquitquit", "POST");
  EXPECT_TRUE(waitForEnvoyToExit());
}

// This test is identical to the above one, except that instead of using an admin /quitquitquit,
// we send ourselves a SIGTERM, which should have the same effect.
TEST_F(MainCommonTest, AdminRequestGetStatsAndKill) {
  if (!Envoy::TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    return;
  }
  addArg("--disable-hot-restart");
  startEnvoy();
  waitForEnvoyToStart();
  EXPECT_THAT(adminRequest("/stats", "GET"), HasSubstr("filesystem.reopen_failed"));
  kill(getpid(), SIGTERM);
  EXPECT_TRUE(waitForEnvoyToExit());
}

} // namespace Envoy

#endif // ENVOY_CONFIG_COVERAGE
