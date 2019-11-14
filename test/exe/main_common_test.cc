#include "envoy/common/platform.h"

#include "common/common/lock_guard.h"
#include "common/common/mutex_tracer_impl.h"
#include "common/common/thread.h"
#include "common/runtime/runtime_impl.h"

#include "exe/main_common.h"

#include "server/options_impl.h"

#include "test/test_common/contention.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "common/signal/signal_action.h"
#endif

#include "absl/synchronization/notification.h"

using testing::HasSubstr;
using testing::IsEmpty;

namespace Envoy {

/**
 * Captures common functions needed for invoking MainCommon. Generates a
 * unique --base-id setting based on the pid and a random number. Maintains
 * an argv array that is terminated with nullptr. Identifies the config
 * file relative to runfiles directory.
 */
class MainCommonTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  MainCommonTest()
      : config_file_(TestEnvironment::temporaryFileSubstitute(
            "test/config/integration/google_com_proxy_port_0.v2.yaml", TestEnvironment::ParamMap(),
            TestEnvironment::PortMap(), GetParam())),
        random_string_(fmt::format("{}", computeBaseId())),
        argv_({"envoy-static", "--base-id", random_string_.c_str(), "-c", config_file_.c_str(),
               nullptr}) {}

  /**
   * Computes a numeric ID to incorporate into the names of
   * shared-memory segments and domain sockets, to help keep them
   * distinct from other tests that might be running concurrently.
   *
   * The PID is needed to isolate namespaces between concurrent
   * processes in CI. The random number generator is needed
   * sequentially executed test methods fail with an error in
   * bindDomainSocket if the same base-id is re-used.
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

  // Adds options to make Envoy exit immediately after initialization.
  void initOnly() {
    addArg("--mode");
    addArg("init_only");
  }

  std::string config_file_;
  std::string random_string_;
  std::vector<const char*> argv_;
};

// Exercise the codepath to instantiate MainCommon and destruct it, with hot restart.
TEST_P(MainCommonTest, ConstructDestructHotRestartEnabled) {
  VERBOSE_EXPECT_NO_THROW(MainCommon main_common(argc(), argv()));
}

// Exercise the codepath to instantiate MainCommon and destruct it, without hot restart.
TEST_P(MainCommonTest, ConstructDestructHotRestartDisabled) {
  addArg("--disable-hot-restart");
  VERBOSE_EXPECT_NO_THROW(MainCommon main_common(argc(), argv()));
}

// Exercise init_only explicitly.
TEST_P(MainCommonTest, ConstructDestructHotRestartDisabledNoInit) {
  addArg("--disable-hot-restart");
  initOnly();
  MainCommon main_common(argc(), argv());
  EXPECT_TRUE(main_common.run());
}

// Test that std::set_new_handler() was called and the callback functions as expected.
// This test fails under TSAN and ASAN, so don't run it in that build:
//   [  DEATH   ] ==845==ERROR: ThreadSanitizer: requested allocation size 0x3e800000000
//   exceeds maximum supported size of 0x10000000000
//
//   [  DEATH   ] ==33378==ERROR: AddressSanitizer: requested allocation size 0x3e800000000
//   (0x3e800001000 after adjustments for alignment, red zones etc.) exceeds maximum supported size
//   of 0x10000000000 (thread T0)

class MainCommonDeathTest : public MainCommonTest {};

TEST_P(MainCommonDeathTest, OutOfMemoryHandler) {
#if defined(__has_feature) && (__has_feature(thread_sanitizer) || __has_feature(address_sanitizer))
  ENVOY_LOG_MISC(critical,
                 "MainCommonTest::OutOfMemoryHandler not supported by this compiler configuration");
#else
  MainCommon main_common(argc(), argv());
  EXPECT_DEATH_LOG_TO_STDERR(
      []() {
        // Resolving symbols for a backtrace takes longer than the timeout in coverage builds,
        // so disable handling that signal.
        signal(SIGABRT, SIG_DFL);

        // Allocating a fixed-size large array that results in OOM on gcc
        // results in a compile-time error on clang of "array size too big",
        // so dynamically find a size that is too large.
        const uint64_t initial = 1 << 30;
        for (uint64_t size = initial;
             size >= initial; // Disallow wraparound to avoid infinite loops on failure.
             size *= 1000) {
          new int[size];
        }
      }(),
      ".*panic: out of memory.*");
#endif
}

INSTANTIATE_TEST_SUITE_P(IpVersions, MainCommonTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

class AdminRequestTest : public MainCommonTest {
protected:
  AdminRequestTest() { addArg("--disable-hot-restart"); }

  // Runs an admin request specified in path, blocking until completion, and
  // returning the response body.
  std::string adminRequest(absl::string_view path, absl::string_view method) {
    absl::Notification done;
    std::string out;
    main_common_->adminRequest(
        path, method,
        [&done, &out](const Http::HeaderMap& /*response_headers*/, absl::string_view body) {
          out = std::string(body);
          done.Notify();
        });
    done.WaitForNotification();
    return out;
  }

  // Initiates Envoy running in its own thread.
  void startEnvoy() {
    envoy_thread_ = Thread::threadFactoryForTest().createThread([this]() {
      // Note: main_common_ is accessed in the testing thread, but
      // is race-free, as MainCommon::run() does not return until
      // triggered with an adminRequest POST to /quitquitquit, which
      // is done in the testing thread.
      main_common_ = std::make_unique<MainCommon>(argc(), argv());
      envoy_started_ = true;
      started_.Notify();
      pauseResumeInterlock(pause_before_run_);
      bool status = main_common_->run();
      pauseResumeInterlock(pause_after_run_);
      main_common_.reset();
      envoy_finished_ = true;
      envoy_return_ = status;
      finished_.Notify();
    });
  }

  // Conditionally pauses at a critical point in the Envoy thread, waiting for
  // the test thread to trigger something at that exact line. The test thread
  // can then call resume_.Notify() to allow the Envoy thread to resume.
  void pauseResumeInterlock(bool enable) {
    if (enable) {
      pause_point_.Notify();
      resume_.WaitForNotification();
    }
  }

  // Wait until Envoy is inside the main server run loop proper. Before entering, Envoy runs any
  // pending post callbacks, so it's not reliable to use adminRequest() or post() to do this.
  // Generally, tests should not depend on this for correctness, but as a result of
  // https://github.com/libevent/libevent/issues/779 we need to for TSAN. This is because the entry
  // to event_base_loop() is where the signal base race occurs, but once we're in that loop in
  // blocking mode, we're safe to take signals.
  // TODO(htuch): Remove when https://github.com/libevent/libevent/issues/779 is fixed.
  void waitForEnvoyRun() {
    absl::Notification done;
    main_common_->dispatcherForTest().post([this, &done] {
      struct Sacrifice : Event::DeferredDeletable {
        Sacrifice(absl::Notification& notify) : notify_(notify) {}
        ~Sacrifice() override { notify_.Notify(); }
        absl::Notification& notify_;
      };
      auto sacrifice = std::make_unique<Sacrifice>(done);
      // Wait for a deferred delete cleanup, this only happens in the main server run loop.
      main_common_->dispatcherForTest().deferredDelete(std::move(sacrifice));
    });
    done.WaitForNotification();
  }

  // Having triggered Envoy to quit (via signal or /quitquitquit), this blocks until Envoy exits.
  bool waitForEnvoyToExit() {
    finished_.WaitForNotification();
    envoy_thread_->join();
    return envoy_return_;
  }

  Stats::IsolatedStoreImpl stats_store_;
  std::unique_ptr<Thread::Thread> envoy_thread_;
  std::unique_ptr<MainCommon> main_common_;
  absl::Notification started_;
  absl::Notification finished_;
  absl::Notification resume_;
  absl::Notification pause_point_;
  bool envoy_return_{false};
  bool envoy_started_{false};
  bool envoy_finished_{false};
  bool pause_before_run_{false};
  bool pause_after_run_{false};
};

TEST_P(AdminRequestTest, AdminRequestGetStatsAndQuit) {
  startEnvoy();
  started_.WaitForNotification();
  EXPECT_THAT(adminRequest("/stats", "GET"), HasSubstr("filesystem.reopen_failed"));
  adminRequest("/quitquitquit", "POST");
  EXPECT_TRUE(waitForEnvoyToExit());
}

// This test is identical to the above one, except that instead of using an admin /quitquitquit,
// we send ourselves a SIGTERM, which should have the same effect.
TEST_P(AdminRequestTest, AdminRequestGetStatsAndKill) {
  startEnvoy();
  started_.WaitForNotification();
  // TODO(htuch): Remove when https://github.com/libevent/libevent/issues/779 is
  // fixed, started_ will then become our real synchronization point.
  waitForEnvoyRun();
  EXPECT_THAT(adminRequest("/stats", "GET"), HasSubstr("filesystem.reopen_failed"));
  kill(getpid(), SIGTERM);
  EXPECT_TRUE(waitForEnvoyToExit());
}

// This test is the same as AdminRequestGetStatsAndQuit, except we send ourselves a SIGINT,
// equivalent to receiving a Ctrl-C from the user.
TEST_P(AdminRequestTest, AdminRequestGetStatsAndCtrlC) {
  startEnvoy();
  started_.WaitForNotification();
  // TODO(htuch): Remove when https://github.com/libevent/libevent/issues/779 is
  // fixed, started_ will then become our real synchronization point.
  waitForEnvoyRun();
  EXPECT_THAT(adminRequest("/stats", "GET"), HasSubstr("filesystem.reopen_failed"));
  kill(getpid(), SIGINT);
  EXPECT_TRUE(waitForEnvoyToExit());
}

TEST_P(AdminRequestTest, AdminRequestContentionDisabled) {
  startEnvoy();
  started_.WaitForNotification();
  // TODO(htuch): Remove when https://github.com/libevent/libevent/issues/779 is
  // fixed, started_ will then become our real synchronization point.
  waitForEnvoyRun();
  EXPECT_THAT(adminRequest("/contention", "GET"), HasSubstr("not enabled"));
  kill(getpid(), SIGTERM);
  EXPECT_TRUE(waitForEnvoyToExit());
}

TEST_P(AdminRequestTest, AdminRequestContentionEnabled) {
  addArg("--enable-mutex-tracing");
  startEnvoy();
  started_.WaitForNotification();
  // TODO(htuch): Remove when https://github.com/libevent/libevent/issues/779 is
  // fixed, started_ will then become our real synchronization point.
  waitForEnvoyRun();

  // Induce contention to guarantee a non-zero num_contentions count.
  Thread::TestUtil::ContentionGenerator contention_generator(main_common_->server()->api());
  contention_generator.generateContention(MutexTracerImpl::getOrCreateTracer());

  std::string response = adminRequest("/contention", "GET");
  EXPECT_THAT(response, Not(HasSubstr("not enabled")));
  EXPECT_THAT(response, HasSubstr("\"num_contentions\":"));
  EXPECT_THAT(response, Not(HasSubstr("\"num_contentions\": \"0\"")));

  kill(getpid(), SIGTERM);
  EXPECT_TRUE(waitForEnvoyToExit());
}

TEST_P(AdminRequestTest, AdminRequestBeforeRun) {
  // Induce the situation where the Envoy thread is active, and main_common_ is constructed,
  // but run() hasn't been issued yet. AdminRequests will not finish immediately, but will
  // do so at some point after run() is allowed to start.
  pause_before_run_ = true;
  startEnvoy();
  pause_point_.WaitForNotification();

  bool admin_handler_was_called = false;
  std::string out;
  main_common_->adminRequest(
      "/stats", "GET",
      [&admin_handler_was_called, &out](const Http::HeaderMap& /*response_headers*/,
                                        absl::string_view body) {
        admin_handler_was_called = true;
        out = std::string(body);
      });

  // The admin handler can't be called until after we let run() go.
  EXPECT_FALSE(admin_handler_was_called);
  EXPECT_THAT(out, IsEmpty());

  // Now unblock the envoy thread so it can wake up and process outstanding posts.
  resume_.Notify();

  // We don't get a notification when run(), so it's not safe to check whether the
  // admin handler is called until after we quit.
  adminRequest("/quitquitquit", "POST");
  EXPECT_TRUE(waitForEnvoyToExit());
  EXPECT_TRUE(admin_handler_was_called);

  // This just checks that some stat output was reported. We could pick any stat.
  EXPECT_THAT(out, HasSubstr("filesystem.reopen_failed"));
}

// Class to track whether an object has been destroyed, which it does by bumping an atomic.
class DestroyCounter {
public:
  // Note: destroy_count is captured by reference, so the variable must last longer than
  // the DestroyCounter.
  explicit DestroyCounter(std::atomic<uint64_t>& destroy_count) : destroy_count_(destroy_count) {}
  ~DestroyCounter() { ++destroy_count_; }

private:
  std::atomic<uint64_t>& destroy_count_;
};

TEST_P(AdminRequestTest, AdminRequestAfterRun) {
  startEnvoy();
  started_.WaitForNotification();
  // Induce the situation where Envoy is no longer in run(), but hasn't been
  // destroyed yet. AdminRequests will never finish, but they won't crash.
  pause_after_run_ = true;
  adminRequest("/quitquitquit", "POST");
  pause_point_.WaitForNotification(); // run() finished, but main_common_ still exists.

  // Admin requests will not work, but will never complete. The lambda itself will be
  // destroyed on thread exit, which we'll track with an object that counts destructor calls.

  std::atomic<uint64_t> lambda_destroy_count(0);
  bool admin_handler_was_called = false;
  {
    // Ownership of the tracker will be passed to the lambda.
    auto tracker = std::make_shared<DestroyCounter>(lambda_destroy_count);
    main_common_->adminRequest(
        "/stats", "GET",
        [&admin_handler_was_called, tracker](const Http::HeaderMap& /*response_headers*/,
                                             absl::string_view /*body*/) {
          admin_handler_was_called = true;
          UNREFERENCED_PARAMETER(tracker);
        });
  }
  EXPECT_EQ(0, lambda_destroy_count); // The lambda won't be destroyed till envoy thread exit.

  // Now unblock the envoy thread so it can destroy the object, along with our unfinished
  // admin request.
  resume_.Notify();

  EXPECT_TRUE(waitForEnvoyToExit());
  EXPECT_FALSE(admin_handler_was_called);
  EXPECT_EQ(1, lambda_destroy_count);
}

// Verifies that the Logger::Registry is usable after constructing and
// destructing MainCommon.
TEST_P(MainCommonTest, ConstructDestructLogger) {
  VERBOSE_EXPECT_NO_THROW(MainCommon main_common(argc(), argv()));

  const std::string logger_name = "logger";
  spdlog::details::log_msg log_msg(&logger_name, spdlog::level::level_enum::err, "error");
  Logger::Registry::getSink()->log(log_msg);
}

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminRequestTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace Envoy
