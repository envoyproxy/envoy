// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <netinet/in.h>

#include <fstream>

#include "source/common/memory/stats.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"
#include "source/common/quic/platform/quiche_flags_impl.h"

#include "test/common/buffer/utility.h"
#include "test/common/quic/platform/quic_epoll_clock.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/transport_sockets/tls/ssl_test_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "fmt/printf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/epoll_server/fake_simple_epoll_server.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_client_stats.h"
#include "quiche/quic/platform/api/quic_containers.h"
#include "quiche/quic/platform/api/quic_expect_bug.h"
#include "quiche/quic/platform/api/quic_exported_stats.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_hostname_utils.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_mem_slice.h"
#include "quiche/quic/platform/api/quic_mem_slice_span.h"
#include "quiche/quic/platform/api/quic_mem_slice_storage.h"
#include "quiche/quic/platform/api/quic_mock_log.h"
#include "quiche/quic/platform/api/quic_mutex.h"
#include "quiche/quic/platform/api/quic_server_stats.h"
#include "quiche/quic/platform/api/quic_sleep.h"
#include "quiche/quic/platform/api/quic_stack_trace.h"
#include "quiche/quic/platform/api/quic_stream_buffer_allocator.h"
#include "quiche/quic/platform/api/quic_system_event_loop.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/platform/api/quic_test_output.h"
#include "quiche/quic/platform/api/quic_thread.h"

// Basic tests to validate functioning of the QUICHE quic platform
// implementation. For platform APIs in which the implementation is a simple
// typedef/passthrough to a std:: or absl:: construct, the tests are kept
// minimal, and serve primarily to verify the APIs compile and link without
// issue.

using testing::_;
using testing::HasSubstr;

namespace quic {
namespace {

class QuicPlatformTest : public testing::Test {
protected:
  QuicPlatformTest()
      : log_level_(GetLogger().level()), verbosity_log_threshold_(getVerbosityLogThreshold()) {
    setVerbosityLogThreshold(0);
    GetLogger().set_level(spdlog::level::err);
  }

  void SetUp() override { Envoy::Assert::resetEnvoyBugCountersForTest(); }
  ~QuicPlatformTest() override {
    setVerbosityLogThreshold(verbosity_log_threshold_);
    GetLogger().set_level(log_level_);
  }

  const QuicLogLevel log_level_;
  const int verbosity_log_threshold_;
};

enum class TestEnum { ZERO = 0, ONE, TWO, COUNT };

TEST_F(QuicPlatformTest, QuicBugTracker) {
  EXPECT_ENVOY_BUG(QUIC_BUG(bug_id) << "Here is a bug,", " bug");
  EXPECT_ENVOY_BUG(QUIC_BUG_IF(bug_id, 1 == 1) << "There is a bug,", " bug");
  bool evaluated = false;
  EXPECT_LOG_NOT_CONTAINS(
      "error", "", QUIC_BUG_IF(bug_id_1, false) << "A feature is not a bug." << (evaluated = true));
  EXPECT_FALSE(evaluated);

  {
    ScopedDisableExitOnQuicheBug no_crash_quiche_bug;
    QUIC_BUG(bug_id_2) << "No crash bug";
  }

  EXPECT_LOG_CONTAINS("error", " bug", QUIC_PEER_BUG(bug_id) << "Everywhere's a bug,");
  EXPECT_LOG_CONTAINS("error", " here", QUIC_PEER_BUG_IF(bug_id, true) << "Including here.");
  EXPECT_LOG_NOT_CONTAINS("error", "", QUIC_PEER_BUG_IF(bug_id, false) << "But not there.");
}

TEST_F(QuicPlatformTest, QuicClientStats) {
  // Just make sure they compile.
  QUIC_CLIENT_HISTOGRAM_ENUM("my.enum.histogram", TestEnum::ONE, TestEnum::COUNT, "doc");
  QUIC_CLIENT_HISTOGRAM_BOOL("my.bool.histogram", false, "doc");
  QUIC_CLIENT_HISTOGRAM_TIMES("my.timing.histogram", QuicTime::Delta::FromSeconds(5),
                              QuicTime::Delta::FromSeconds(1), QuicTime::Delta::FromSecond(3600),
                              100, "doc");
  QUIC_CLIENT_HISTOGRAM_COUNTS("my.count.histogram", 123, 0, 1000, 100, "doc");
  QuicClientSparseHistogram("my.sparse.histogram", 345);
  // Make sure compiler doesn't report unused-parameter error.
  bool should_be_used;
  QUIC_CLIENT_HISTOGRAM_BOOL("my.bool.histogram", should_be_used, "doc");
}

TEST_F(QuicPlatformTest, QuicExpectBug) {
  auto bug = [](const char* error_message) { QUIC_BUG(bug_id) << error_message; };

  auto peer_bug = [](const char* error_message) { QUIC_PEER_BUG(bug_id) << error_message; };
  EXPECT_QUIC_BUG(bug("bug one is expected"), "bug one");
  EXPECT_QUIC_BUG(bug("bug two is expected"), "bug two");
#ifdef NDEBUG
  // The 3rd triggering in release mode should not be logged.
  EXPECT_LOG_NOT_CONTAINS("error", "bug three", bug("bug three is expected"));
#else
  EXPECT_QUIC_BUG(bug("bug three is expected"), "bug three");
#endif

  EXPECT_QUIC_PEER_BUG(peer_bug("peer_bug_1 is expected"), "peer_bug_1");
  EXPECT_QUIC_PEER_BUG(peer_bug("peer_bug_2 is expected"), "peer_bug_2");
}

TEST_F(QuicPlatformTest, QuicExportedStats) {
  // Just make sure they compile.
  QUIC_HISTOGRAM_ENUM("my.enum.histogram", TestEnum::ONE, TestEnum::COUNT, "doc");
  QUIC_HISTOGRAM_BOOL("my.bool.histogram", false, "doc");
  QUIC_HISTOGRAM_TIMES("my.timing.histogram", QuicTime::Delta::FromSeconds(5),
                       QuicTime::Delta::FromSeconds(1), QuicTime::Delta::FromSecond(3600), 100,
                       "doc");
  QUIC_HISTOGRAM_COUNTS("my.count.histogram", 123, 0, 1000, 100, "doc");
}

TEST_F(QuicPlatformTest, QuicHostnameUtils) {
  EXPECT_FALSE(QuicHostnameUtils::IsValidSNI("!!"));
  EXPECT_FALSE(QuicHostnameUtils::IsValidSNI("envoyproxy"));
  EXPECT_TRUE(QuicHostnameUtils::IsValidSNI("www.envoyproxy.io"));
  EXPECT_EQ("lyft.com", QuicHostnameUtils::NormalizeHostname("lyft.com"));
  EXPECT_EQ("google.com", QuicHostnameUtils::NormalizeHostname("google.com..."));
  EXPECT_EQ("quicwg.org", QuicHostnameUtils::NormalizeHostname("QUICWG.ORG"));
}

TEST_F(QuicPlatformTest, QuicMockLog) {
  ASSERT_EQ(spdlog::level::err, GetLogger().level());

  {
    // Test a mock log that is not capturing logs.
    CREATE_QUIC_MOCK_LOG(log);
    EXPECT_QUIC_LOG_CALL(log).Times(0);
    QUIC_LOG(ERROR) << "This should be logged but not captured by the mock.";
  }

  // Test nested mock logs.
  CREATE_QUIC_MOCK_LOG(outer_log);
  outer_log.StartCapturingLogs();

  {
    // Test a mock log that captures logs.
    CREATE_QUIC_MOCK_LOG(inner_log);
    inner_log.StartCapturingLogs();

    EXPECT_QUIC_LOG_CALL_CONTAINS(inner_log, ERROR, "Inner log message");
    QUIC_LOG(ERROR) << "Inner log message should be captured.";

    // Destruction of inner_log should restore the QUIC log sink to outer_log.
  }

  EXPECT_QUIC_LOG_CALL_CONTAINS(outer_log, ERROR, "Outer log message");
  QUIC_LOG(ERROR) << "Outer log message should be captured.";
}

TEST_F(QuicPlatformTest, QuicServerStats) {
  // Just make sure they compile.
  QUIC_SERVER_HISTOGRAM_ENUM("my.enum.histogram", TestEnum::ONE, TestEnum::COUNT, "doc");
  QUIC_SERVER_HISTOGRAM_BOOL("my.bool.histogram", false, "doc");
  QUIC_SERVER_HISTOGRAM_TIMES("my.timing.histogram", QuicTime::Delta::FromSeconds(5),
                              QuicTime::Delta::FromSeconds(1), QuicTime::Delta::FromSecond(3600),
                              100, "doc");
  QUIC_SERVER_HISTOGRAM_COUNTS("my.count.histogram", 123, 0, 1000, 100, "doc");
}

TEST_F(QuicPlatformTest, QuicStackTraceTest) {
#if !defined(ENVOY_CONFIG_COVERAGE) && !defined(GCC_COMPILER)
  // This doesn't work in coverage build because part of the stacktrace will be overwritten by
  // __llvm_coverage_mapping
  // Stack trace under gcc with optimizations on (-c opt) doesn't include the test name
  EXPECT_THAT(QuicStackTrace(), HasSubstr("QuicStackTraceTest"));
#endif
}

TEST_F(QuicPlatformTest, QuicSleep) { QuicSleep(QuicTime::Delta::FromMilliseconds(20)); }

TEST_F(QuicPlatformTest, QuicThread) {
  class AdderThread : public QuicThread {
  public:
    AdderThread(int* value, int increment)
        : QuicThread("adder_thread"), value_(value), increment_(increment) {}

    ~AdderThread() override = default;

  protected:
    void Run() override { *value_ += increment_; }

  private:
    int* value_;
    int increment_;
  };

  int value = 0;

  // A QuicThread that is never started, which is ok.
  { AdderThread t0(&value, 1); }
  EXPECT_EQ(0, value);

  // A QuicThread that is started and joined as usual.
  {
    AdderThread t1(&value, 1);
    t1.Start();
    t1.Join();
  }
  EXPECT_EQ(1, value);

  // QuicThread will panic if it's started but not joined.
  EXPECT_DEATH({ AdderThread(&value, 2).Start(); },
               "QuicThread should be joined before destruction");
}

TEST_F(QuicPlatformTest, QuicLog) {
  // By default, tests emit logs at level ERROR or higher.
  ASSERT_EQ(spdlog::level::err, GetLogger().level());

  int i = 0;

  QUIC_LOG(INFO) << (i = 10);
  QUIC_LOG_IF(INFO, false) << i++;
  QUIC_LOG_IF(INFO, true) << i++;
  EXPECT_EQ(0, i);

  EXPECT_LOG_CONTAINS("error", "i=11", QUIC_LOG(ERROR) << "i=" << (i = 11));
  EXPECT_EQ(11, i);

  QUIC_LOG_IF(ERROR, false) << i++;
  EXPECT_EQ(11, i);

  EXPECT_LOG_CONTAINS("error", "i=11", QUIC_LOG_IF(ERROR, true) << "i=" << i++);
  EXPECT_EQ(12, i);

  // Set QUIC log level to INFO, since VLOG is emitted at the INFO level.
  GetLogger().set_level(spdlog::level::info);

  ASSERT_EQ(0, getVerbosityLogThreshold());

  QUIC_VLOG(1) << (i = 1);
  EXPECT_EQ(12, i);

  setVerbosityLogThreshold(1);

  EXPECT_LOG_CONTAINS("info", "i=1", QUIC_VLOG(1) << "i=" << (i = 1));
  EXPECT_EQ(1, i);

  errno = SOCKET_ERROR_INVAL;
  EXPECT_LOG_CONTAINS("info", "i=3:", QUIC_PLOG(INFO) << "i=" << (i = 3));
  EXPECT_EQ(3, i);

  char* null_string = nullptr;
  EXPECT_LOG_CONTAINS("error", "null_string=(null)",
                      QUIC_LOG(ERROR) << "null_string=" << null_string);
  const char* const_null_string = nullptr;
  EXPECT_LOG_CONTAINS("error", "const_null_string=(null)",
                      QUIC_LOG(ERROR) << "const_null_string=" << const_null_string);
  EXPECT_LOG_CONTAINS("error", "nullptr=(null)", QUIC_LOG(ERROR) << "nullptr=" << nullptr);
}

#ifdef NDEBUG
#define VALUE_BY_COMPILE_MODE(debug_mode_value, release_mode_value) release_mode_value
#else
#define VALUE_BY_COMPILE_MODE(debug_mode_value, release_mode_value) debug_mode_value
#endif

TEST_F(QuicPlatformTest, LogIoManipulators) {
  GetLogger().set_level(spdlog::level::err);
  QUIC_DLOG(ERROR) << "aaaa" << std::endl;
  EXPECT_LOG_CONTAINS("error", "aaaa\n\n", QUIC_LOG(ERROR) << "aaaa" << std::endl << std::endl);
  EXPECT_LOG_NOT_CONTAINS("error", "aaaa\n\n\n",
                          QUIC_LOG(ERROR) << "aaaa" << std::endl
                                          << std::endl);

  EXPECT_LOG_CONTAINS("error", "42 in octal is 52",
                      QUIC_LOG(ERROR) << 42 << " in octal is " << std::oct << 42);
}

TEST_F(QuicPlatformTest, QuicDLog) {
  int i = 0;

  GetLogger().set_level(spdlog::level::err);

  QUIC_DLOG(INFO) << (i = 10);
  QUIC_DLOG_IF(INFO, false) << i++;
  QUIC_DLOG_IF(INFO, true) << i++;
  EXPECT_EQ(0, i);

  GetLogger().set_level(spdlog::level::info);

  QUIC_DLOG(INFO) << (i = 10);
  QUIC_DLOG_IF(INFO, false) << i++;
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(10, 0), i);

  QUIC_DLOG_IF(INFO, true) << (i = 11);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(11, 0), i);

  ASSERT_EQ(0, getVerbosityLogThreshold());

  QUIC_DVLOG(1) << (i = 1);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(11, 0), i);

  setVerbosityLogThreshold(1);

  QUIC_DVLOG(1) << (i = 1);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(1, 0), i);

  QUIC_DVLOG_IF(1, false) << (i = 2);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(1, 0), i);

  QUIC_DVLOG_IF(1, true) << (i = 2);
  EXPECT_EQ(VALUE_BY_COMPILE_MODE(2, 0), i);
}

#undef VALUE_BY_COMPILE_MODE

TEST_F(QuicPlatformTest, QuicheCheck) {
  QUICHE_CHECK(1 == 1);
  QUICHE_CHECK(1 == 1) << " 1 == 1 is forever true.";

  EXPECT_DEBUG_DEATH({ QUICHE_DCHECK(false) << " Supposed to fail in debug mode."; },
                     "CHECK failed:.* Supposed to fail in debug mode.");
  EXPECT_DEBUG_DEATH({ QUICHE_DCHECK(false); }, "CHECK failed");

  EXPECT_DEATH({ QUICHE_CHECK(false) << " Supposed to fail in all modes."; },
               "CHECK failed:.* Supposed to fail in all modes.");
  EXPECT_DEATH({ QUICHE_CHECK(false); }, "CHECK failed");
  EXPECT_DEATH({ QUICHE_CHECK_LT(1 + 1, 2); }, "CHECK failed: 1 \\+ 1 \\(=2\\) < 2 \\(=2\\)");
  EXPECT_DEBUG_DEATH({ QUICHE_DCHECK_NE(1 + 1, 2); },
                     "CHECK failed: 1 \\+ 1 \\(=2\\) != 2 \\(=2\\)");
  EXPECT_DEBUG_DEATH({ QUICHE_DCHECK_NE(nullptr, nullptr); },
                     "CHECK failed: nullptr \\(=\\(null\\)\\) != nullptr \\(=\\(null\\)\\)");
}

// Test the behaviors of the cross products of
//
//   {QUIC_LOG, QUIC_DLOG} x {FATAL, DFATAL} x {debug, release}
TEST_F(QuicPlatformTest, QuicFatalLog) {
#ifdef NDEBUG
  // Release build
  EXPECT_DEATH(QUIC_LOG(FATAL) << "Should abort 0", "Should abort 0");
  QUIC_LOG(DFATAL) << "Should not abort";
  QUIC_DLOG(FATAL) << "Should compile out";
  QUIC_DLOG(DFATAL) << "Should compile out";
#else
  // Debug build
  EXPECT_DEATH(QUIC_LOG(FATAL) << "Should abort 1", "Should abort 1");
  EXPECT_DEATH(QUIC_LOG(DFATAL) << "Should abort 2", "Should abort 2");
  EXPECT_DEATH(QUIC_DLOG(FATAL) << "Should abort 3", "Should abort 3");
  EXPECT_DEATH(QUIC_DLOG(DFATAL) << "Should abort 4", "Should abort 4");
#endif
}

TEST_F(QuicPlatformTest, QuicBranchPrediction) {
  GetLogger().set_level(spdlog::level::info);

  if (QUIC_PREDICT_FALSE(rand() % RAND_MAX == 123456789)) {
    QUIC_LOG(INFO) << "Go buy some lottery tickets.";
  } else {
    QUIC_LOG(INFO) << "As predicted.";
  }
}

TEST_F(QuicPlatformTest, QuicNotReached) {
#ifdef NDEBUG
  QUIC_NOTREACHED(); // Expect no-op.
#else
  EXPECT_DEATH(QUIC_NOTREACHED(), "not reached");
#endif
}

TEST_F(QuicPlatformTest, QuicMutex) {
  QuicMutex mu;

  QuicWriterMutexLock wmu(&mu);
  mu.AssertReaderHeld();
  mu.WriterUnlock();
  {
    QuicReaderMutexLock rmu(&mu);
    mu.AssertReaderHeld();
  }
  mu.WriterLock();
}

TEST_F(QuicPlatformTest, QuicNotification) {
  QuicNotification notification;
  EXPECT_FALSE(notification.HasBeenNotified());
  notification.Notify();
  notification.WaitForNotification();
  EXPECT_TRUE(notification.HasBeenNotified());
}

TEST_F(QuicPlatformTest, QuicTestOutput) {
  Envoy::TestEnvironment::setEnvVar("QUIC_TEST_OUTPUT_DIR", "/tmp", /*overwrite=*/false);

  // Set log level to INFO to see the test output path in log.
  GetLogger().set_level(spdlog::level::info);

  EXPECT_LOG_NOT_CONTAINS("warn", "", QuicRecordTrace("quic_test_output.1", "output 1 content\n"));
  EXPECT_LOG_NOT_CONTAINS("error", "", QuicRecordTrace("quic_test_output.2", "output 2 content\n"));
  EXPECT_LOG_CONTAINS("info", "Recorded test output into",
                      QuicRecordTrace("quic_test_output.3", "output 3 content\n"));

  std::string content4{"output 4 content\n"};
  const testing::TestInfo* test_info = testing::UnitTest::GetInstance()->current_test_info();

  std::string timestamp = absl::FormatTime("%Y%m%d%H%M%S", absl::Now(), absl::LocalTimeZone());

  std::string filename = fmt::sprintf("%s.%s.%s.%s.qtr", test_info->name(),
                                      test_info->test_case_name(), "quic_test_output.4", timestamp);

  EXPECT_LOG_CONTAINS("info", "Recorded test output into", QuicSaveTestOutput(filename, content4));

  std::string content;
  EXPECT_TRUE(QuicLoadTestOutput(filename, &content));
  EXPECT_EQ("output 4 content\n", content);
  EXPECT_FALSE(QuicLoadTestOutput("nonexisting_file", &content));
}

TEST_F(QuicPlatformTest, ApproximateNowInUsec) {
  epoll_server::test::FakeSimpleEpollServer epoll_server;
  QuicEpollClock clock(&epoll_server);

  epoll_server.set_now_in_usec(1000000);
  EXPECT_EQ(1000000, (clock.ApproximateNow() - QuicTime::Zero()).ToMicroseconds());
  EXPECT_EQ(1u, clock.WallNow().ToUNIXSeconds());
  EXPECT_EQ(1000000u, clock.WallNow().ToUNIXMicroseconds());

  epoll_server.AdvanceBy(5);
  EXPECT_EQ(1000005, (clock.ApproximateNow() - QuicTime::Zero()).ToMicroseconds());
  EXPECT_EQ(1u, clock.WallNow().ToUNIXSeconds());
  EXPECT_EQ(1000005u, clock.WallNow().ToUNIXMicroseconds());

  epoll_server.AdvanceBy(10 * 1000000);
  EXPECT_EQ(11u, clock.WallNow().ToUNIXSeconds());
  EXPECT_EQ(11000005u, clock.WallNow().ToUNIXMicroseconds());
}

TEST_F(QuicPlatformTest, NowInUsec) {
  epoll_server::test::FakeSimpleEpollServer epoll_server;
  QuicEpollClock clock(&epoll_server);

  epoll_server.set_now_in_usec(1000000);
  EXPECT_EQ(1000000, (clock.Now() - QuicTime::Zero()).ToMicroseconds());

  epoll_server.AdvanceBy(5);
  EXPECT_EQ(1000005, (clock.Now() - QuicTime::Zero()).ToMicroseconds());
}

TEST_F(QuicPlatformTest, MonotonicityWithRealEpollClock) {
  epoll_server::SimpleEpollServer epoll_server;
  QuicEpollClock clock(&epoll_server);

  quic::QuicTime last_now = clock.Now();
  for (int i = 0; i < 1e5; ++i) {
    quic::QuicTime now = clock.Now();

    ASSERT_LE(last_now, now);

    last_now = now;
  }
}

TEST_F(QuicPlatformTest, MonotonicityWithFakeEpollClock) {
  epoll_server::test::FakeSimpleEpollServer epoll_server;
  QuicEpollClock clock(&epoll_server);

  epoll_server.set_now_in_usec(100);
  quic::QuicTime last_now = clock.Now();

  epoll_server.set_now_in_usec(90);
  quic::QuicTime now = clock.Now();

  ASSERT_EQ(last_now, now);
}

TEST_F(QuicPlatformTest, QuicFlags) {
  auto& flag_registry = quiche::FlagRegistry::getInstance();
  flag_registry.resetFlags();

  EXPECT_FALSE(GetQuicReloadableFlag(quic_testonly_default_false));
  EXPECT_TRUE(GetQuicReloadableFlag(quic_testonly_default_true));
  SetQuicReloadableFlag(quic_testonly_default_false, true);
  EXPECT_TRUE(GetQuicReloadableFlag(quic_testonly_default_false));

  EXPECT_FALSE(GetQuicRestartFlag(quic_testonly_default_false));
  EXPECT_TRUE(GetQuicRestartFlag(quic_testonly_default_true));
  SetQuicRestartFlag(quic_testonly_default_false, true);
  EXPECT_TRUE(GetQuicRestartFlag(quic_testonly_default_false));

  EXPECT_EQ(200, GetQuicFlag(FLAGS_quic_time_wait_list_seconds));
  SetQuicFlag(FLAGS_quic_time_wait_list_seconds, 100);
  EXPECT_EQ(100, GetQuicFlag(FLAGS_quic_time_wait_list_seconds));

  flag_registry.resetFlags();
  EXPECT_FALSE(GetQuicReloadableFlag(quic_testonly_default_false));
  EXPECT_TRUE(GetQuicRestartFlag(quic_testonly_default_true));
  EXPECT_EQ(200, GetQuicFlag(FLAGS_quic_time_wait_list_seconds));
  flag_registry.findFlag("FLAGS_quic_reloadable_flag_quic_testonly_default_false")
      ->setValueFromString("true");
  flag_registry.findFlag("FLAGS_quic_restart_flag_quic_testonly_default_true")
      ->setValueFromString("0");
  flag_registry.findFlag("FLAGS_quic_time_wait_list_seconds")->setValueFromString("100");
  EXPECT_TRUE(GetQuicReloadableFlag(quic_testonly_default_false));
  EXPECT_FALSE(GetQuicRestartFlag(quic_testonly_default_true));
  EXPECT_EQ(100, GetQuicFlag(FLAGS_quic_time_wait_list_seconds));
}

class FileUtilsTest : public testing::Test {
public:
  FileUtilsTest() : dir_path_(Envoy::TestEnvironment::temporaryPath("quic_file_util_test")) {
    files_to_remove_.push(dir_path_);
  }

protected:
  void SetUp() override { Envoy::TestEnvironment::createPath(dir_path_); }

  void TearDown() override {
    while (!files_to_remove_.empty()) {
      const std::string& f = files_to_remove_.top();
      Envoy::TestEnvironment::removePath(f);
      files_to_remove_.pop();
    }
  }

  void addSubDirs(std::list<std::string> sub_dirs) {
    for (const std::string& dir_name : sub_dirs) {
      const std::string full_path = dir_path_ + "/" + dir_name;
      Envoy::TestEnvironment::createPath(full_path);
      files_to_remove_.push(full_path);
    }
  }

  void addFiles(std::list<std::string> files) {
    for (const std::string& file_name : files) {
      const std::string full_path = dir_path_ + "/" + file_name;
      { const std::ofstream file(full_path); }
      files_to_remove_.push(full_path);
    }
  }

  const std::string dir_path_;
  std::stack<std::string> files_to_remove_;
};

TEST_F(QuicPlatformTest, TestEnvoyQuicBufferAllocator) {
  QuicStreamBufferAllocator allocator;
  Envoy::Stats::TestUtil::MemoryTest memory_test;
  if (memory_test.mode() == Envoy::Stats::TestUtil::MemoryTest::Mode::Disabled) {
    return;
  }
  char* p = allocator.New(1024);
  EXPECT_NE(nullptr, p);
  EXPECT_GT(memory_test.consumedBytes(), 0);
  memset(p, 'a', 1024);
  allocator.Delete(p);
  EXPECT_EQ(memory_test.consumedBytes(), 0);
}

TEST_F(QuicPlatformTest, TestSystemEventLoop) {
  // These two interfaces are no-op in Envoy. The test just makes sure they
  // build.
  QuicRunSystemEventLoopIteration();
  QuicSystemEventLoop("dummy");
}

TEST(EnvoyQuicMemSliceTest, ConstructMemSliceFromBuffer) {
  std::string str(512, 'b');
  // Fragment needs to out-live buffer.
  bool fragment_releaser_called = false;
  Envoy::Buffer::BufferFragmentImpl fragment(
      str.data(), str.length(),
      [&fragment_releaser_called](const void*, size_t, const Envoy::Buffer::BufferFragmentImpl*) {
        // Used to verify that mem slice release appropriately.
        fragment_releaser_called = true;
      });
  Envoy::Buffer::OwnedImpl buffer;
  EXPECT_DEBUG_DEATH(quic::QuicMemSlice slice0{quic::QuicMemSliceImpl(buffer, 0)}, "");
  std::string str2(1024, 'a');
  // str2 is copied.
  buffer.add(str2);
  EXPECT_EQ(1u, buffer.getRawSlices().size());
  buffer.addBufferFragment(fragment);

  quic::QuicMemSlice slice1{quic::QuicMemSliceImpl(buffer, str2.length())};
  EXPECT_EQ(str.length(), buffer.length());
  EXPECT_EQ(str2, std::string(slice1.data(), slice1.length()));
  std::string str2_old = str2; // NOLINT(performance-unnecessary-copy-initialization)
  // slice1 is released, but str2 should not be affected.
  slice1.Reset();
  EXPECT_TRUE(slice1.empty());
  EXPECT_EQ(nullptr, slice1.data());
  EXPECT_EQ(str2_old, str2);

  quic::QuicMemSlice slice2{quic::QuicMemSliceImpl(buffer, str.length())};
  EXPECT_EQ(0, buffer.length());
  EXPECT_EQ(str.data(), slice2.data());
  EXPECT_EQ(str, std::string(slice2.data(), slice2.length()));
  slice2.Reset();
  EXPECT_TRUE(slice2.empty());
  EXPECT_EQ(nullptr, slice2.data());
  EXPECT_TRUE(fragment_releaser_called);
}

TEST(EnvoyQuicMemSliceTest, ConstructQuicMemSliceSpan) {
  Envoy::Buffer::OwnedImpl buffer;
  std::string str(1024, 'a');
  buffer.add(str);
  quic::QuicMemSlice slice{quic::QuicMemSliceImpl(buffer, str.length())};

  QuicMemSliceSpan span(&slice);
  EXPECT_EQ(1024u, span.total_length());
  EXPECT_EQ(str, span.GetData(0));
  span.ConsumeAll([](quic::QuicMemSlice&& mem_slice) { mem_slice.Reset(); });
  EXPECT_EQ(0u, span.total_length());

  QuicMemSlice slice3;
  {
    quic::QuicMemSlice slice2{quic::QuicMemSliceImpl(std::make_unique<char[]>(5), 5u)};

    QuicMemSliceSpan span2(&slice2);
    EXPECT_EQ(5u, span2.total_length());
    span2.ConsumeAll([&slice3](quic::QuicMemSlice&& mem_slice) { slice3 = std::move(mem_slice); });
    EXPECT_EQ(0u, span2.total_length());
  }
  slice3.Reset();
}

TEST(EnvoyQuicMemSliceTest, QuicMemSliceStorage) {
  std::string str(512, 'a');
  iovec iov = {const_cast<char*>(str.data()), str.length()};
  SimpleBufferAllocator allocator;
  QuicMemSliceStorage storage(&iov, 1, &allocator, 1024);
  // Test copy constructor.
  QuicMemSliceStorage other = storage;
  QuicMemSliceSpan span = storage.ToSpan();
  EXPECT_EQ(1u, span.NumSlices());
  EXPECT_EQ(str.length(), span.total_length());
  EXPECT_EQ(str, span.GetData(0));
  QuicMemSliceSpan span_other = other.ToSpan();
  EXPECT_EQ(1u, span_other.NumSlices());
  EXPECT_EQ(str, span_other.GetData(0));
  EXPECT_NE(span_other.GetData(0).data(), span.GetData(0).data());
}

} // namespace
} // namespace quic
