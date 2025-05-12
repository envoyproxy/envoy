#include <string>

#include "source/common/common/fine_grain_logger.h"
#include "source/common/common/logger.h"

#include "test/test_common/logging.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(FineGrainLog, safeFileNameMatch) {
  // Exact file name or path match.
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("foo", "foo"));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("foo", "bar"));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("foo", "fo"));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("foo", "foo1"));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("bar/baz.cc", "bar/foo.cc"));

  // Star wildcard match.
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("*ba*r/f*oo.c*c", "bar/foo.cc"));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("*/*.cc", "barbaz/foo.cc2"));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("bar/*.cc", "barbaz/foo.cc"));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("/*", "foo/bar.h"));
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("/*", "/bar.h"));

  // Question wildcard match.
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("?a?/?", "bar/"));
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("??r/?", "bar/a"));

  // Question and star wildcards match.
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("b?r/*", "bar/foo.cc2"));
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("b?r/*", "bar/"));
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("?a?/*.cc", "bar/foo.cc"));
}

TEST(FineGrainLog, updateCurrentFilePath) {
  Logger::Context::enableFineGrainLogger();
  FINE_GRAIN_LOG(critical, "critical: initialize a fine grain logger for test.");
  getFineGrainLogContext().setFineGrainLogger(__FILE__, spdlog::level::info);
  FINE_GRAIN_LOG(debug, "Debug: you shouldn't see this message!");

  const std::pair<absl::string_view, int> update =
      std::make_pair(__FILE__, static_cast<int>(spdlog::level::debug));
  getFineGrainLogContext().updateVerbositySetting({update});

  FINE_GRAIN_LOG(debug, "Debug: now level is debug");
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
}

TEST(FineGrainLog, verbosityDefaultLevelUpdate) {
  getFineGrainLogContext().setFineGrainLogger(__FILE__, spdlog::level::info);
  FINE_GRAIN_LOG(info, "Info: verbosityDefaultLevelUpdate test begins.");
  FINE_GRAIN_LOG(debug, "Debug: you shouldn't see this message!");

  EXPECT_EQ(getFineGrainLogContext().getVerbosityDefaultLevel(),
            Logger::Context::getFineGrainDefaultLevel());

  // Clear verbosity update info at first.
  getFineGrainLogContext().updateVerbositySetting({});
  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::debug);

  FINE_GRAIN_LOG(debug, "Debug: now level is debug");
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);

  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::debug);
  p = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  EXPECT_EQ(p->level(), spdlog::level::debug);
}

TEST(FineGrainLog, verbosityUpdatePriority) {
  getFineGrainLogContext().setFineGrainLogger(__FILE__, spdlog::level::info);
  FINE_GRAIN_LOG(info, "Info: verbosityUpdatePriority test begins.");
  FINE_GRAIN_LOG(debug, "Debug: you shouldn't see this message!");
  // Clear verbosity update info at first.

  absl::string_view file_path = __FILE__;
  file_path.remove_suffix(3);
  const std::pair<absl::string_view, int> update =
      std::make_pair(file_path, static_cast<int>(spdlog::level::debug));
  getFineGrainLogContext().updateVerbositySetting({update});
  // This will also try to clear the verbosity update by changing the default level.
  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::trace);

  FINE_GRAIN_LOG(trace, "Trace: you should see this message");
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::trace);
}

TEST(FineGrainLog, updateBasename) {
  Logger::Context::enableFineGrainLogger();
  getFineGrainLogContext().setFineGrainLogger(__FILE__, spdlog::level::info);
  FINE_GRAIN_LOG(info, "Info: verbosityUpdateBasename test begins.");
  FINE_GRAIN_LOG(debug, "Debug: you shouldn't see this message!");

  absl::string_view file_path = __FILE__;
  file_path.remove_suffix(3);
  const size_t position = file_path.rfind('/');
  file_path.remove_prefix(position + 1);

  const std::pair<absl::string_view, int> update =
      std::make_pair(file_path, static_cast<int>(spdlog::level::debug));
  getFineGrainLogContext().updateVerbositySetting({update});

  FINE_GRAIN_LOG(debug, "Debug: now level is debug");
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(__FILE__);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
}

TEST(FineGrainLog, multipleUpdatesBasename) {
  const std::string file_1 = "envoy/src/foo.cc";
  const std::string file_2 = "envoy/src/bar.cc";
  static std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, flogger);
  getFineGrainLogContext().initFineGrainLogger(file_2, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("foo", static_cast<int>(spdlog::level::debug));
  const std::pair<absl::string_view, int> update_2 =
      std::make_pair("bar", static_cast<int>(spdlog::level::warn));
  getFineGrainLogContext().updateVerbositySetting({update_1, update_2});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(FineGrainLog, multipleMatchesBasename) {
  const std::string file_1 = "envoy/src/a.cc";
  const std::string file_2 = "envoy/a.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, flogger);
  getFineGrainLogContext().initFineGrainLogger(file_2, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("a", static_cast<int>(spdlog::level::warn));
  getFineGrainLogContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(FineGrainLog, globStarUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  const std::string file_2 = "envoy/src/bar.cc";
  const std::string file_3 = "envoy/baz.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, flogger);
  getFineGrainLogContext().initFineGrainLogger(file_2, flogger);
  getFineGrainLogContext().initFineGrainLogger(file_3, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("envoy/*", static_cast<int>(spdlog::level::warn));
  getFineGrainLogContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_3);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);

  const std::pair<absl::string_view, int> update_2 =
      std::make_pair("envoy/src/*", static_cast<int>(spdlog::level::info));
  getFineGrainLogContext().updateVerbositySetting({update_2});

  p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_3);
  EXPECT_EQ(p->level(), getFineGrainLogContext().getVerbosityDefaultLevel());

  const std::pair<absl::string_view, int> update_3 =
      std::make_pair("*/src/*", static_cast<int>(spdlog::level::err));
  getFineGrainLogContext().updateVerbositySetting({update_3});

  p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::err);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::err);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_3);
  EXPECT_EQ(p->level(), getFineGrainLogContext().getVerbosityDefaultLevel());

  const std::pair<absl::string_view, int> update_4 =
      std::make_pair("*", static_cast<int>(spdlog::level::info));
  getFineGrainLogContext().updateVerbositySetting({update_4});

  p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_3);
  EXPECT_EQ(p->level(), spdlog::level::info);

  const std::pair<absl::string_view, int> update_5 =
      std::make_pair("/*", static_cast<int>(spdlog::level::debug));
  getFineGrainLogContext().updateVerbositySetting({update_5});

  p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  EXPECT_EQ(p->level(), getFineGrainLogContext().getVerbosityDefaultLevel());
  p = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  EXPECT_EQ(p->level(), getFineGrainLogContext().getVerbosityDefaultLevel());
  p = getFineGrainLogContext().getFineGrainLogEntry(file_3);
  EXPECT_EQ(p->level(), getFineGrainLogContext().getVerbosityDefaultLevel());
}

TEST(FineGrainLog, globQuestionUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  const std::string file_2 = "envoy/f__.cc";
  const std::string file_3 = "/bar.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, flogger);
  getFineGrainLogContext().initFineGrainLogger(file_2, flogger);
  getFineGrainLogContext().initFineGrainLogger(file_3, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("f??", static_cast<int>(spdlog::level::warn));
  getFineGrainLogContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_3);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), getFineGrainLogContext().getVerbosityDefaultLevel());

  const std::pair<absl::string_view, int> update_2 =
      std::make_pair("????y/*", static_cast<int>(spdlog::level::info));
  getFineGrainLogContext().updateVerbositySetting({update_2});
  p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFineGrainLogContext().getFineGrainLogEntry(file_3);
  EXPECT_EQ(p->level(), getFineGrainLogContext().getVerbosityDefaultLevel());
}

TEST(FineGrainLog, inOrderGlobUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("f??", static_cast<int>(spdlog::level::warn));
  const std::pair<absl::string_view, int> update_2 =
      std::make_pair("envoy/*/f??", static_cast<int>(spdlog::level::info));
  getFineGrainLogContext().updateVerbositySetting({update_1, update_2});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

// This is to verify the memory optimization to avoid storing patterns that will
// never match due to exit early semantics.
TEST(FineGrainLog, earlyExitGlobUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, flogger);

  // f?? appears before ff?.
  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("f??", static_cast<int>(spdlog::level::warn));
  const std::pair<absl::string_view, int> update_2 =
      std::make_pair("ff?", static_cast<int>(spdlog::level::info));
  getFineGrainLogContext().updateVerbositySetting({update_1, update_2});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(FineGrainLog, invalidGlobLevelUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, flogger);

  const std::pair<absl::string_view, int> update_1 = std::make_pair("f??", 9);
  getFineGrainLogContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), getFineGrainLogContext().getVerbosityDefaultLevel());
}

} // namespace Envoy
