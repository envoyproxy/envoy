#include "envoy/common/platform.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/mutex_tracer_impl.h"
#include "source/common/common/random_generator.h"
#include "source/common/common/thread.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/exe/main_common.h"
#include "source/exe/platform_impl.h"
#include "source/server/options_impl.h"

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
class MainCommonTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  MainCommonTest()
      : config_file_(TestEnvironment::temporaryFileSubstitute(
            "test/config/integration/google_com_proxy_port_0.yaml", TestEnvironment::ParamMap(),
            TestEnvironment::PortMap(), GetParam())),
        argv_({"envoy-static", "--use-dynamic-base-id", "-c", config_file_.c_str(), nullptr}) {}

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
  std::vector<const char*> argv_;
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

  void quitAndWait() {
    adminRequest("/quitquitquit", "POST");
    EXPECT_TRUE(waitForEnvoyToExit());
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

class AdminStreamingTest : public AdminRequestTest {
protected:
  static constexpr absl::string_view StreamingEndpoint = "/stream";

  class StreamingAdminRequest : public Envoy::Server::Admin::Request {
  public:
    static constexpr uint64_t NumChunks = 10;
    static constexpr uint64_t BytesPerChunk = 10000;

    /*StreamingAdminRequest(OptRef<absl::Notification>& pause_chunk)
        : chunk_(BytesPerChunk, 'a'),
        pause_chunk_(pause_chunk) {}*/
    StreamingAdminRequest(std::function<void()>& next_chunk_hook)
        : chunk_(BytesPerChunk, 'a'), next_chunk_hook_(next_chunk_hook) {}
    Http::Code start(Http::ResponseHeaderMap&) override { return Http::Code::OK; }
    bool nextChunk(Buffer::Instance& response) override {
      next_chunk_hook_();
      response.add(chunk_);
      return --chunks_remaining_ > 0;
    }

  private:
    const std::string chunk_;
    uint64_t chunks_remaining_{NumChunks};
    std::function<void()>& next_chunk_hook_;
  };

  AdminStreamingTest() {
    startEnvoy();
    started_.WaitForNotification();
    Server::Admin& admin = *main_common_->server()->admin();
    admin.addStreamingHandler(
        std::string(StreamingEndpoint), "streaming api",
        [this](Server::AdminStream&) -> Server::Admin::RequestPtr {
          return std::make_unique<StreamingAdminRequest>(next_chunk_hook_);
        },
        true, false);
  }

  using ChunksBytes = std::pair<uint64_t, uint64_t>;
  ChunksBytes runStreamingRequest(MainCommonBase::AdminResponseSharedPtr response,
                                  std::function<void()> chunk_hook = nullptr) {
    absl::Notification done;
    std::vector<std::string> out;
    absl::Notification headers_notify;
    response->getHeaders(
        [&headers_notify](Http::Code, Http::ResponseHeaderMap&) { headers_notify.Notify(); });
    headers_notify.WaitForNotification();
    bool cont = true;
    uint64_t num_chunks = 0;
    uint64_t num_bytes = 0;
    while (cont && !response->cancelled()) {
      absl::Notification chunk_notify;
      response->nextChunk(
          [&chunk_notify, &num_chunks, &num_bytes, &cont](Buffer::Instance& chunk, bool more) {
            cont = more;
            num_bytes += chunk.length();
            chunk.drain(chunk.length());
            ++num_chunks;
            chunk_notify.Notify();
          });
      chunk_notify.WaitForNotification();
      if (chunk_hook != nullptr) {
        chunk_hook();
      }
    }

    return ChunksBytes(num_chunks, num_bytes);
  }

  /**
   * In order to trigger certain early-exit criteria in a test, we can exploit
   * the fact that all the admin responses are delivered on the main thread.
   * So we can pause those by blocking the main thread indefinitely.
   *
   * To resume the main thread, call resume_.Notify();
   *
   * @param url the stats endpoint to initiate.
   */
  void blockMainThreadUntilResume(absl::string_view url, absl::string_view method) {
    MainCommonBase::AdminResponseSharedPtr blocked_response =
        main_common_->adminRequest(url, method);
    absl::Notification block_main_thread;
    blocked_response->getHeaders(
        [this](Http::Code, Http::ResponseHeaderMap&) { resume_.WaitForNotification(); });
  }

  std::function<void()> next_chunk_hook_ = []() {};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminStreamingTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminStreamingTest, RequestGetStatsAndQuit) {
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  ChunksBytes chunks_bytes = runStreamingRequest(response);
  EXPECT_EQ(StreamingAdminRequest::NumChunks, chunks_bytes.first);
  EXPECT_EQ(StreamingAdminRequest::NumChunks * StreamingAdminRequest::BytesPerChunk,
            chunks_bytes.second);
  quitAndWait();
}

TEST_P(AdminStreamingTest, QuitDuringChunks) {
  int quit_counter = 0;
  static constexpr int chunks_to_send_before_quitting = 3;
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  ChunksBytes chunks_bytes = runStreamingRequest(response, [&quit_counter, this]() {
    if (++quit_counter == chunks_to_send_before_quitting) {
      quitAndWait();
    }
  });
  EXPECT_EQ(4, chunks_bytes.first);
  EXPECT_EQ(chunks_to_send_before_quitting * StreamingAdminRequest::BytesPerChunk,
            chunks_bytes.second);
}

TEST_P(AdminStreamingTest, CancelDuringChunks) {
  int quit_counter = 0;
  static constexpr int chunks_to_send_before_quitting = 3;
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  ChunksBytes chunks_bytes = runStreamingRequest(response, [response, &quit_counter]() {
    if (++quit_counter == chunks_to_send_before_quitting) {
      response->cancel();
    }
  });
  EXPECT_EQ(3, chunks_bytes.first); // no final call to the chunk handler after cancel.
  EXPECT_EQ(chunks_to_send_before_quitting * StreamingAdminRequest::BytesPerChunk,
            chunks_bytes.second);
  quitAndWait();
}

TEST_P(AdminStreamingTest, CancelBeforeAskingForHeader) {
  blockMainThreadUntilResume(StreamingEndpoint, "GET");
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  response->cancel();
  resume_.Notify();
  int header_calls = 0;

  // After 'cancel', the headers function will not be called.
  response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
  quitAndWait();
  EXPECT_EQ(0, header_calls);
}

TEST_P(AdminStreamingTest, CancelAfterAskingForHeader) {
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  int header_calls = 0;
  blockMainThreadUntilResume(StreamingEndpoint, "GET");
  response->getHeaders([&header_calls](Http::Code, Http::ResponseHeaderMap&) { ++header_calls; });
  response->cancel();
  resume_.Notify();
  quitAndWait();
  EXPECT_EQ(0, header_calls);
}

TEST_P(AdminStreamingTest, CancelBeforeAskingForChunk) {
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  absl::Notification headers_notify;
  response->getHeaders(
      [&headers_notify](Http::Code, Http::ResponseHeaderMap&) { headers_notify.Notify(); });
  headers_notify.WaitForNotification();

  // To hit an early exit in AdminResponseImpl::requestNextChunk we need to
  // allow its posting to the main thread, but not let it run. To do that we
  // queue up a main-thread callback that blocks on a notification, thus pausing
  // Envoy's main thread.
  absl::Notification block_cancel;
  main_common_->dispatcherForTest().post([response, &block_cancel] {
    block_cancel.WaitForNotification();
    response->cancel();
  });

  int chunk_calls = 0;
  response->nextChunk([&chunk_calls](Buffer::Instance&, bool) { ++chunk_calls; });

  // Now that we have issued the request for the nextChunk call, we can unblock
  // our callback, which will hit the early exit in the chunk handler.
  block_cancel.Notify();
  quitAndWait();
  EXPECT_EQ(0, chunk_calls);
}

TEST_P(AdminStreamingTest, CancelAfterAskingForChunk) {
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  absl::Notification headers_notify;
  response->getHeaders(
      [&headers_notify](Http::Code, Http::ResponseHeaderMap&) { headers_notify.Notify(); });
  headers_notify.WaitForNotification();
  blockMainThreadUntilResume(StreamingEndpoint, "GET");
  int chunk_calls = 0;

  // Cause the /streaming handler to pause while yielding the next chunk, to hit
  // an early exit in MainCommonBase::requestNextChunk.
  next_chunk_hook_ = [response]() { response->cancel(); };
  response->nextChunk([&chunk_calls](Buffer::Instance&, bool) { ++chunk_calls; });
  resume_.Notify();
  quitAndWait();
  EXPECT_EQ(0, chunk_calls);
}

TEST_P(AdminStreamingTest, QuitBeforeHeaders) {
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  quitAndWait();
  ChunksBytes chunks_bytes = runStreamingRequest(response);
  EXPECT_EQ(1, chunks_bytes.first);
  EXPECT_EQ(0, chunks_bytes.second);
}

TEST_P(AdminStreamingTest, QuitDeleteRace) {
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  // Initiates a streaming quit on the main thread, but do not wait for it.
  MainCommonBase::AdminResponseSharedPtr quit_response =
      main_common_->adminRequest("/quitquitquit", "POST");
  quit_response->getHeaders([](Http::Code, Http::ResponseHeaderMap&) {});
  response.reset(); // Races with the quitquitquit
  EXPECT_TRUE(waitForEnvoyToExit());
}

TEST_P(AdminStreamingTest, QuitCancelRace) {
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  // Initiates a streaming quit on the main thread, but do not wait for it.
  MainCommonBase::AdminResponseSharedPtr quit_response =
      main_common_->adminRequest("/quitquitquit", "POST");
  quit_response->getHeaders([](Http::Code, Http::ResponseHeaderMap&) {});
  response->cancel(); // Races with the quitquitquit
  EXPECT_TRUE(waitForEnvoyToExit());
}

TEST_P(AdminStreamingTest, QuitBeforeCreatingResponse) {
  // Initiates a streaming quit on the main thread, and wait for headers, which
  // will trigger the termination of the event loop, and subsequent nulling of
  // main_common_. However we can pause the test infrastructure after the quit
  // takes hold leaving main_common_ in tact, to reproduce a potential race.
  pause_after_run_ = true;
  adminRequest("/quitquitquit", "POST");
  pause_point_.WaitForNotification(); // run() finished, but main_common_ still exists.
  MainCommonBase::AdminResponseSharedPtr response =
      main_common_->adminRequest(StreamingEndpoint, "GET");
  ChunksBytes chunks_bytes = runStreamingRequest(response);
  EXPECT_EQ(1, chunks_bytes.first);
  EXPECT_EQ(0, chunks_bytes.second);
  resume_.Notify();
  EXPECT_TRUE(waitForEnvoyToExit());
  response.reset();
}

} // namespace Envoy
