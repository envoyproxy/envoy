#include "envoy/common/platform.h"

#include "source/common/common/mutex_tracer_impl.h"
#include "source/common/common/random_generator.h"
#include "source/common/common/thread.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/exe/main_common.h"
#include "source/exe/platform_impl.h"
#include "source/server/options_impl.h"

#include "test/exe/main_common_test_base.h"
#include "test/mocks/common.h"
#include "test/test_common/contention.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#ifdef ENVOY_HANDLE_SIGNALS
#include "source/common/signal/signal_action.h"
#endif

#include "absl/synchronization/notification.h"

using testing::HasSubstr;
using testing::IsEmpty;
using testing::NiceMock;
using testing::Return;

namespace Envoy {

namespace {

#if !(defined(__clang_analyzer__) ||                                                               \
      (defined(__has_feature) &&                                                                   \
       (__has_feature(thread_sanitizer) || __has_feature(address_sanitizer) ||                     \
        __has_feature(memory_sanitizer))))
const std::string& outOfMemoryPattern() {
#if defined(TCMALLOC)
  CONSTRUCT_ON_FIRST_USE(std::string, ".*Unable to allocate.*");
#else
  CONSTRUCT_ON_FIRST_USE(std::string, ".*panic: out of memory.*");
#endif
}
#endif

} // namespace

/**
 * Captures common functions needed for invoking MainCommon.Maintains
 * an argv array that is terminated with nullptr. Identifies the config
 * file relative to runfiles directory.
 */
class MainCommonTest : public MainCommonTestBase,
                       public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  MainCommonTest() : MainCommonTestBase(GetParam()) {}
};
INSTANTIATE_TEST_SUITE_P(IpVersions, MainCommonTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

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

// This test verifies that validation can run from MainCommonBase, from a thread
// other than the test thread. This is a desirable calling sequence in some
// contexts. To make this work we must declare MainThread in MainCommonBase, in
// addition to declaring it in MainCommon. There is no harm in double-declaring.
TEST_P(MainCommonTest, ValidateUsingMainCommonBaseOutsideTestThread) {
  EXPECT_FALSE(Thread::MainThread::isMainThreadActive());
  const char* argv[] = {"envoy-static",       "--mode", "validate", "--config-path",
                        config_file_.c_str(), nullptr};
  Envoy::OptionsImpl options(ARRAY_SIZE(argv) - 1, argv, &MainCommon::hotRestartVersion,
                             spdlog::level::info);
  std::unique_ptr<Thread::Thread> thread =
      Thread::threadFactoryForTest().createThread([&options]() {
        Event::TestRealTimeSystem real_time_system;
        DefaultListenerHooks default_listener_hooks;
        ProdComponentFactory prod_component_factory;
        MainCommonBase base(options, real_time_system, default_listener_hooks,
                            prod_component_factory, std::make_unique<PlatformImpl>(),
                            std::make_unique<Random::RandomGeneratorImpl>(), nullptr);
        EXPECT_TRUE(base.run());
      });
  thread->join();
  EXPECT_FALSE(Thread::MainThread::isMainThreadActive());
}

TEST_P(MainCommonTest, ConstructDestructHotRestartDisabledNoInitWithVectorArgs) {
  addArg("--disable-hot-restart");
  initOnly();
  std::vector<std::string> args(argv_.size());
  for (size_t i = 0; i < argv_.size() - 1; ++i) {
    args[i] = std::string(argv_[i]);
  }
  MainCommon main_common(args);
  EXPECT_TRUE(main_common.run());
}

// Exercise base-id-path option.
TEST_P(MainCommonTest, ConstructWritesBasePathId) {
#ifdef ENVOY_HOT_RESTART
  const std::string base_id_path = TestEnvironment::temporaryPath("base-id-file");
  addArg("--base-id-path");
  addArg(base_id_path.c_str());
  VERBOSE_EXPECT_NO_THROW(MainCommon main_common(argc(), argv()));

  EXPECT_NE("", TestEnvironment::readFileToStringForTest(base_id_path));
#endif
}

// Exercise enabling core dump and succeeding.
// Note: this test will call the real system call, which is what we want.
TEST_P(MainCommonTest, EnableCoreDump) {
#ifdef __linux__
  addArg("--enable-core-dump");
  auto test = [&]() { MainCommon main_common(argc(), argv()); };
  EXPECT_LOG_CONTAINS("info", "core dump enabled", test());
#endif
}

class MockPlatformImpl : public PlatformImpl {
public:
  MOCK_METHOD(bool, enableCoreDump, ());
};

// Exercise enabling core dump and failing.
TEST_P(MainCommonTest, EnableCoreDumpFails) {
  Event::TestRealTimeSystem real_time_system;
  DefaultListenerHooks default_listener_hooks;
  ProdComponentFactory prod_component_factory;

  const auto args = std::vector<std::string>(
      {"envoy-static", "--use-dynamic-base-id", "-c", config_file_, "--enable-core-dump"});
  OptionsImpl options(args, &MainCommon::hotRestartVersion, spdlog::level::info);

  auto test = [&]() {
    auto* platform_impl = new NiceMock<MockPlatformImpl>();
    EXPECT_CALL(*platform_impl, enableCoreDump()).WillOnce(Return(false));
    MainCommonBase first(options, real_time_system, default_listener_hooks, prod_component_factory,
                         std::unique_ptr<PlatformImpl>{platform_impl},
                         std::make_unique<Random::RandomGeneratorImpl>(), nullptr);
  };

  EXPECT_NO_THROW(test());
  EXPECT_LOG_CONTAINS("warn", "failed to enable core dump", test());
}

// Test that an in-use base id triggers a retry and that we eventually give up.
TEST_P(MainCommonTest, RetryDynamicBaseIdFails) {
#ifdef ENVOY_HOT_RESTART
  Event::TestRealTimeSystem real_time_system;
  DefaultListenerHooks default_listener_hooks;
  ProdComponentFactory prod_component_factory;

  const std::string base_id_path = TestEnvironment::temporaryPath("base-id-file");

  const auto first_args = std::vector<std::string>({"envoy-static", "--use-dynamic-base-id", "-c",
                                                    config_file_, "--base-id-path", base_id_path});
  OptionsImpl first_options(first_args, &MainCommon::hotRestartVersion, spdlog::level::info);
  MainCommonBase first(first_options, real_time_system, default_listener_hooks,
                       prod_component_factory, std::make_unique<PlatformImpl>(),
                       std::make_unique<Random::RandomGeneratorImpl>(), nullptr);

  const std::string base_id_str = TestEnvironment::readFileToStringForTest(base_id_path);
  uint32_t base_id;
  ASSERT_TRUE(absl::SimpleAtoi(base_id_str, &base_id));

  auto* mock_rng = new NiceMock<Random::MockRandomGenerator>();
  EXPECT_CALL(*mock_rng, random()).WillRepeatedly(Return(base_id));

  const auto second_args =
      std::vector<std::string>({"envoy-static", "--use-dynamic-base-id", "-c", config_file_});
  OptionsImpl second_options(second_args, &MainCommon::hotRestartVersion, spdlog::level::info);

  EXPECT_THROW_WITH_MESSAGE(MainCommonBase(second_options, real_time_system, default_listener_hooks,
                                           prod_component_factory, std::make_unique<PlatformImpl>(),
                                           std::unique_ptr<Random::RandomGenerator>{mock_rng},
                                           nullptr),
                            EnvoyException, "unable to select a dynamic base id");
#endif
}

// Verifies that the Logger::Registry is usable after constructing and
// destructing MainCommon.
TEST_P(MainCommonTest, ConstructDestructLogger) {
  VERBOSE_EXPECT_NO_THROW(MainCommon main_common(argc(), argv()));

  const std::string logger_name = "logger";
  spdlog::details::log_msg log_msg(logger_name, spdlog::level::level_enum::err, "error");
  Logger::Registry::getSink()->log(log_msg);
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
INSTANTIATE_TEST_SUITE_P(IpVersions, MainCommonDeathTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(MainCommonDeathTest, OutOfMemoryHandler) {
#if defined(__clang_analyzer__) || (defined(__has_feature) && (__has_feature(thread_sanitizer) ||  \
                                                               __has_feature(address_sanitizer) || \
                                                               __has_feature(memory_sanitizer)))
  ENVOY_LOG_MISC(critical,
                 "MainCommonTest::OutOfMemoryHandler not supported by this compiler configuration");
#else
  MainCommon main_common(argc(), argv());
#if !defined(WIN32)
  // Resolving symbols for a backtrace takes longer than the timeout in coverage builds,
  // so disable handling that signal.
  signal(SIGABRT, SIG_DFL);
#endif
  EXPECT_DEATH(
      []() {
        // Allocating a fixed-size large array that results in OOM on gcc
        // results in a compile-time error on clang of "array size too big",
        // so dynamically find a size that is too large.
        const uint64_t initial = 1 << 30;
        for (uint64_t size = initial;
             size >= initial; // Disallow wraparound to avoid infinite loops on failure.
             size *= 1000) {
          int* p = new int[size];
          // Use the pointer to prevent clang from optimizing the allocation away in opt mode.
          ENVOY_LOG_MISC(debug, "p={}", reinterpret_cast<intptr_t>(p));
        }
      }(),
      outOfMemoryPattern());
#endif
}

class AdminRequestTest : public AdminRequestTestBase,
                         public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  AdminRequestTest() : AdminRequestTestBase(GetParam()) {}
};
INSTANTIATE_TEST_SUITE_P(IpVersions, AdminRequestTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminRequestTest, AdminRequestGetStatsAndQuit) {
  startEnvoy();
  started_.WaitForNotification();
  EXPECT_THAT(adminRequest("/stats", "GET"), HasSubstr("filesystem.reopen_failed"));
  quitAndWait();
}

// no signals on Windows -- could probably make this work with GenerateConsoleCtrlEvent
#ifndef WIN32
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
#endif

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
  quitAndWait();
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

} // namespace Envoy
