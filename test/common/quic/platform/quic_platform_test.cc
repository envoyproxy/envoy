// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <netinet/in.h>

#include <fstream>

#include "source/common/memory/stats.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"

#include "test/common/buffer/utility.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/common/tls/ssl_test_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "fmt/printf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"
#include "quiche/common/quiche_mem_slice_storage.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_client_stats.h"
#include "quiche/quic/platform/api/quic_expect_bug.h"
#include "quiche/quic/platform/api/quic_exported_stats.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_hostname_utils.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_mutex.h"
#include "quiche/quic/platform/api/quic_server_stats.h"
#include "quiche/quic/platform/api/quic_stack_trace.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/platform/api/quic_test_output.h"
#include "quiche/quic/platform/api/quic_thread.h"
#include "quiche_platform_impl/quiche_flags_impl.h"

// Basic tests to validate functioning of the QUICHE quic platform
// implementation. For platform APIs in which the implementation is a simple
// typedef/passthrough to a std:: or absl:: construct, the tests are kept
// minimal, and serve primarily to verify the APIs compile and link without
// issue.

using quiche::GetLogger;
using quiche::getVerbosityLogThreshold;
using quiche::setVerbosityLogThreshold;
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

  quiche::test::QuicheFlagSaver saver_;
  const quiche::QuicheLogLevel log_level_;
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
  // SNI without dot is valid as per RFC 2396.
  EXPECT_TRUE(QuicHostnameUtils::IsValidSNI("envoyproxy"));
  EXPECT_TRUE(QuicHostnameUtils::IsValidSNI("www.envoyproxy.io"));
  EXPECT_EQ("lyft.com", QuicHostnameUtils::NormalizeHostname("lyft.com"));
  EXPECT_EQ("google.com", QuicHostnameUtils::NormalizeHostname("google.com..."));
  EXPECT_EQ("quicwg.org", QuicHostnameUtils::NormalizeHostname("QUICWG.ORG"));
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

// https://github.com/envoyproxy/envoy/issues/26711
TEST_F(QuicPlatformTest, DISABLED_QuicThread) {
  class AdderThread : public QuicThread {
  public:
    AdderThread(int* value, int increment)
        : QuicThread("adder_thread"), value_(value), increment_(increment) {}

    ~AdderThread() override = default;

    void waitForRun() {
      // Wait for Run() to finish.
      absl::MutexLock lk(&m_);
      cv_.Wait(&m_);
    }

  protected:
    void Run() override {
      absl::MutexLock lk(&m_);
      *value_ += increment_;
      cv_.Signal();
    }

  private:
    int* value_;
    int increment_;
    absl::Mutex m_;
    absl::CondVar cv_;
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
  EXPECT_DEATH(
      {
        AdderThread t3(&value, 2);
        t3.Start();
        t3.waitForRun();
      },
      "QuicheThread should be joined before destruction");
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
                     "Check failed:.* Supposed to fail in debug mode.");
  EXPECT_DEBUG_DEATH({ QUICHE_DCHECK(false); }, "Check failed");

  EXPECT_DEATH({ QUICHE_CHECK(false) << " Supposed to fail in all modes."; },
               "Check failed:.* Supposed to fail in all modes.");
  EXPECT_DEATH({ QUICHE_CHECK(false); }, "Check failed");
  EXPECT_DEATH({ QUICHE_CHECK_LT(1 + 1, 2); }, "Check failed: 1 \\+ 1 \\(=2\\) < 2 \\(=2\\)");
  EXPECT_DEBUG_DEATH({ QUICHE_DCHECK_NE(1 + 1, 2); },
                     "Check failed: 1 \\+ 1 \\(=2\\) != 2 \\(=2\\)");
  EXPECT_DEBUG_DEATH({ QUICHE_DCHECK_NE(nullptr, nullptr); },
                     "Check failed: nullptr \\(=\\(null\\)\\) != nullptr \\(=\\(null\\)\\)");
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

TEST_F(QuicPlatformTest, QuicNotReached) {
#ifdef NDEBUG
  QUICHE_NOTREACHED(); // Expect no-op.
#else
  EXPECT_DEATH(QUICHE_NOTREACHED(), "reached unexpected code");
#endif
}

TEST_F(QuicPlatformTest, QuicMutex) {
  QuicMutex mu;

  QuicWriterMutexLock wmu(&mu);
  mu.AssertReaderHeld();
  mu.WriterUnlock();
  {
    quiche::QuicheReaderMutexLock rmu(&mu);
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
  Envoy::TestEnvironment::setEnvVar("QUICHE_TEST_OUTPUT_DIR", "/tmp", /*overwrite=*/false);

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

TEST_F(QuicPlatformTest, QuicFlags) {
  // Test that the flags which envoy explicitly overrides have the right value.
  EXPECT_TRUE(GetQuicReloadableFlag(quic_disable_version_draft_29));
  EXPECT_TRUE(GetQuicReloadableFlag(quic_default_to_bbr));
  EXPECT_FALSE(GetQuicFlag(quic_header_size_limit_includes_overhead));
  EXPECT_EQ(512 * 1024 * 1024, GetQuicFlag(quic_buffered_data_threshold));
  {
    quiche::test::QuicheFlagSaver saver;
    EXPECT_FALSE(GetQuicReloadableFlag(quic_testonly_default_false));
    EXPECT_TRUE(GetQuicReloadableFlag(quic_testonly_default_true));
    SetQuicReloadableFlag(quic_testonly_default_false, true);
    EXPECT_TRUE(GetQuicReloadableFlag(quic_testonly_default_false));

    EXPECT_FALSE(GetQuicRestartFlag(quic_testonly_default_false));
    EXPECT_TRUE(GetQuicRestartFlag(quic_testonly_default_true));
    SetQuicRestartFlag(quic_testonly_default_false, true);
    EXPECT_TRUE(GetQuicRestartFlag(quic_testonly_default_false));

    EXPECT_FALSE(GetQuicheFlag(quiche_oghttp2_debug_trace));
    SetQuicheFlag(quiche_oghttp2_debug_trace, true);
    EXPECT_TRUE(GetQuicheFlag(quiche_oghttp2_debug_trace));

    EXPECT_EQ(200, GetQuicFlag(quic_time_wait_list_seconds));
    SetQuicFlag(quic_time_wait_list_seconds, 100);
    EXPECT_EQ(100, GetQuicFlag(quic_time_wait_list_seconds));
  }

  // Verify that the saver reset all the flags to their previous values.
  EXPECT_FALSE(GetQuicReloadableFlag(quic_testonly_default_false));
  EXPECT_FALSE(GetQuicRestartFlag(quic_testonly_default_false));
  EXPECT_EQ(200, GetQuicFlag(quic_time_wait_list_seconds));
  EXPECT_FALSE(GetQuicheFlag(quiche_oghttp2_debug_trace));
}

TEST_F(QuicPlatformTest, QuicheLogDFatalNoExit) {
  quiche::test::QuicheScopedDisableExitOnDFatal scoped_object;
  QUIC_LOG(DFATAL) << "This shouldn't call abort()";
  QUICHE_DCHECK(false) << "This shouldn't call abort()";
}

TEST_F(QuicPlatformTest, UpdateReloadableFlags) {
  auto& flag_registry = quiche::FlagRegistry::getInstance();

  EXPECT_FALSE(GetQuicReloadableFlag(quic_testonly_default_false));
  EXPECT_TRUE(GetQuicReloadableFlag(quic_testonly_default_true));

  // Flip both flags to a non-default value.
  flag_registry.updateReloadableFlags(
      {{"FLAGS_envoy_quiche_reloadable_flag_quic_testonly_default_false", true},
       {"FLAGS_envoy_quiche_reloadable_flag_quic_testonly_default_true", false}});
  EXPECT_TRUE(GetQuicReloadableFlag(quic_testonly_default_false));
  EXPECT_FALSE(GetQuicReloadableFlag(quic_testonly_default_true));

  // Flip one flag back to a default value.
  flag_registry.updateReloadableFlags(
      {{"FLAGS_envoy_quiche_reloadable_flag_quic_testonly_default_false", false}});
  EXPECT_FALSE(GetQuicReloadableFlag(quic_testonly_default_false));
  EXPECT_FALSE(GetQuicReloadableFlag(quic_testonly_default_true));

  // Flip the other back to a default value.
  flag_registry.updateReloadableFlags(
      {{"FLAGS_envoy_quiche_reloadable_flag_quic_testonly_default_true", true}});
  EXPECT_FALSE(GetQuicReloadableFlag(quic_testonly_default_false));
  EXPECT_TRUE(GetQuicReloadableFlag(quic_testonly_default_true));
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

TEST_F(QuicPlatformTest, TestSystemEventLoop) {
  // These two interfaces are no-op in Envoy. The test just makes sure they
  // build.
  quiche::QuicheRunSystemEventLoopIteration();
  quiche::QuicheSystemEventLoop("dummy");
}

TEST(EnvoyQuicheMemSliceTest, ConstructMemSliceFromBuffer) {
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
  EXPECT_DEBUG_DEATH(quiche::QuicheMemSlice slice0(quiche::QuicheMemSlice::InPlace(), buffer, 0u),
                     "");
  std::string str2(1024, 'a');
  // str2 is copied.
  buffer.add(str2);
  EXPECT_EQ(1u, buffer.getRawSlices().size());
  buffer.addBufferFragment(fragment);

  quiche::QuicheMemSlice slice1(quiche::QuicheMemSlice::InPlace(), buffer, str2.length());
  EXPECT_EQ(str.length(), buffer.length());
  EXPECT_EQ(str2, std::string(slice1.data(), slice1.length()));
  std::string str2_old = str2; // NOLINT(performance-unnecessary-copy-initialization)
  // slice1 is released, but str2 should not be affected.
  slice1.Reset();
  EXPECT_TRUE(slice1.empty());
  EXPECT_EQ(nullptr, slice1.data());
  EXPECT_EQ(str2_old, str2);

  quiche::QuicheMemSlice slice2(quiche::QuicheMemSlice::InPlace(), buffer, str.length());
  EXPECT_EQ(0, buffer.length());
  EXPECT_EQ(str.data(), slice2.data());
  EXPECT_EQ(str, std::string(slice2.data(), slice2.length()));
  slice2.Reset();
  EXPECT_TRUE(slice2.empty());
  EXPECT_EQ(nullptr, slice2.data());
  EXPECT_TRUE(fragment_releaser_called);
}

} // namespace
} // namespace quic
