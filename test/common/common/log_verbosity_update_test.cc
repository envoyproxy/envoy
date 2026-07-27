#include <string>

#include "source/common/common/fine_grain_logger.h"
#include "source/common/common/logger.h"

#include "test/test_common/logging.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::HasSubstr;

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

  getFineGrainLogContext().updateVerbositySetting(
      {{__FILE__, static_cast<int>(spdlog::level::debug)}});
  FINE_GRAIN_LOG(debug, "Debug: now level is debug");
}

TEST(FineGrainLog, verbosityDefaultLevelUpdate) {
  Logger::Context::enableFineGrainLogger();
  FINE_GRAIN_LOG(info, "Info: verbosityDefaultLevelUpdate test begins.");
  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::info);

  getFineGrainLogContext().updateVerbositySetting({});
  FINE_GRAIN_LOG(debug, "Debug: you shouldn't see this message!");

  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::debug);
  FINE_GRAIN_LOG(debug, "Debug: now level is debug");
}

TEST(FineGrainLog, verbosityUpdatePriority) {
  Logger::Context::enableFineGrainLogger();
  FINE_GRAIN_LOG(info, "Info: verbosityUpdatePriority test begins.");
  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::info);

  getFineGrainLogContext().updateVerbositySetting(
      {{__FILE__, static_cast<int>(spdlog::level::trace)}});
  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::info);
  FINE_GRAIN_LOG(trace, "Trace: you should see this message");
}

TEST(FineGrainLog, updateBasename) {
  Logger::Context::enableFineGrainLogger();
  FINE_GRAIN_LOG(info, "Info: verbosityUpdateBasename test begins.");
  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::info);

  getFineGrainLogContext().updateVerbositySetting(
      {{"log_verbosity_update_test", static_cast<int>(spdlog::level::debug)}});
  FINE_GRAIN_LOG(debug, "Debug: now level is debug");
}

TEST(FineGrainLog, multipleUpdatesBasename) {
  const std::string file_1 = "envoy/src/foo.cc";
  const std::string file_2 = "envoy/src/bar.cc";
  const std::string file_3 = "envoy/src/baz.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, "", flogger);
  getFineGrainLogContext().initFineGrainLogger(file_2, "", flogger);
  getFineGrainLogContext().initFineGrainLogger(file_3, "", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"foo", static_cast<int>(spdlog::level::warn)},
       {"bar", static_cast<int>(spdlog::level::debug)}});

  SpdLoggerSharedPtr p1 = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  SpdLoggerSharedPtr p2 = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  SpdLoggerSharedPtr p3 = getFineGrainLogContext().getFineGrainLogEntry(file_3);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);
  ASSERT_NE(p3, nullptr);
  EXPECT_EQ(p1->level(), spdlog::level::warn);
  EXPECT_EQ(p2->level(), spdlog::level::debug);
  EXPECT_EQ(p3->level(), getFineGrainLogContext().getVerbosityDefaultLevel());
}

TEST(FineGrainLog, multipleMatchesBasename) {
  const std::string file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, "", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"foo", static_cast<int>(spdlog::level::warn)},
       {"foo", static_cast<int>(spdlog::level::debug)}});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(FineGrainLog, globStarUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  const std::string file_2 = "envoy/src/bar.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, "", flogger);
  getFineGrainLogContext().initFineGrainLogger(file_2, "", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"envoy/*", static_cast<int>(spdlog::level::warn)}});

  SpdLoggerSharedPtr p1 = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  SpdLoggerSharedPtr p2 = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);
  EXPECT_EQ(p1->level(), spdlog::level::warn);
  EXPECT_EQ(p2->level(), spdlog::level::warn);
}

TEST(FineGrainLog, globQuestionUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  const std::string file_2 = "envoy/src/bar.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, "", flogger);
  getFineGrainLogContext().initFineGrainLogger(file_2, "", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"envoy/src/???.cc", static_cast<int>(spdlog::level::warn)}});

  SpdLoggerSharedPtr p1 = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  SpdLoggerSharedPtr p2 = getFineGrainLogContext().getFineGrainLogEntry(file_2);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);
  EXPECT_EQ(p1->level(), spdlog::level::warn);
  EXPECT_EQ(p2->level(), spdlog::level::warn);
}

TEST(FineGrainLog, inOrderGlobUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, "", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"f??", static_cast<int>(spdlog::level::warn)},
       {"envoy/*/f??", static_cast<int>(spdlog::level::info)}});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(FineGrainLog, earlyExitGlobUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, "", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"envoy/*", static_cast<int>(spdlog::level::warn)},
       {"envoy/src/*", static_cast<int>(spdlog::level::info)}});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(FineGrainLog, invalidGlobLevelUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(file_1, "", flogger);

  getFineGrainLogContext().updateVerbositySetting({{"f??", 9}});

  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(file_1);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), getFineGrainLogContext().getVerbosityDefaultLevel());
}

TEST(FineGrainLog, updateWithLoggerName) {
  Logger::Context::enableFineGrainLogger();
  const std::string key = absl::StrCat(__FILE__, ":misc");
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(__FILE__, "misc", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"misc", static_cast<int>(spdlog::level::debug)}});
  ENVOY_LOG_MISC(debug, "debug message");
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(key);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
}

TEST(FineGrainLog, updateWithBasenameAndLoggerName) {
  Logger::Context::enableFineGrainLogger();
  const std::string key = absl::StrCat(__FILE__, ":misc");
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(__FILE__, "misc", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"log_verbosity_update_test", static_cast<int>(spdlog::level::debug)}});
  ENVOY_LOG_MISC(debug, "debug message");
  SpdLoggerSharedPtr p = getFineGrainLogContext().getFineGrainLogEntry(key);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
}

TEST(FineGrainLog, updateWithGlobAndLoggerName) {
  const std::string key1 = "envoy/src/foo.cc:logger1";
  const std::string key2 = "envoy/src/bar.cc:logger1";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger("envoy/src/foo.cc", "logger1", flogger);
  getFineGrainLogContext().initFineGrainLogger("envoy/src/bar.cc", "logger1", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"logger1", static_cast<int>(spdlog::level::warn)}});

  SpdLoggerSharedPtr p1 = getFineGrainLogContext().getFineGrainLogEntry(key1);
  SpdLoggerSharedPtr p2 = getFineGrainLogContext().getFineGrainLogEntry(key2);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);
  EXPECT_EQ(p1->level(), spdlog::level::warn);
  EXPECT_EQ(p2->level(), spdlog::level::warn);
}

TEST(FineGrainLog, updateWithSameFileAndDifferentLoggerName) {
  const std::string key1 = "envoy/src/foo.cc:logger1";
  const std::string key2 = "envoy/src/foo.cc:logger2";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger("envoy/src/foo.cc", "logger1", flogger);
  getFineGrainLogContext().initFineGrainLogger("envoy/src/foo.cc", "logger2", flogger);

  getFineGrainLogContext().updateVerbositySetting(
      {{"logger1", static_cast<int>(spdlog::level::warn)}});

  SpdLoggerSharedPtr p1 = getFineGrainLogContext().getFineGrainLogEntry(key1);
  SpdLoggerSharedPtr p2 = getFineGrainLogContext().getFineGrainLogEntry(key2);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);
  EXPECT_EQ(p1->level(), spdlog::level::warn);
  EXPECT_EQ(p2->level(), getFineGrainLogContext().getVerbosityDefaultLevel());
}

TEST(FineGrainLog, updateWithLoggerNameOnly) {
  getFineGrainLogContext().updateVerbositySetting({});
  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::info);
  const std::string key1 = "envoy/src/foo.cc:logger1";
  const std::string key2 = "envoy/src/bar.cc:logger1";
  const std::string key3 = "envoy/src/foo.cc:logger2";
  getFineGrainLogContext().removeFineGrainLogEntryForTest(key1);
  getFineGrainLogContext().removeFineGrainLogEntryForTest(key2);
  getFineGrainLogContext().removeFineGrainLogEntryForTest(key3);
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger("envoy/src/foo.cc", "logger1", flogger);
  getFineGrainLogContext().initFineGrainLogger("envoy/src/bar.cc", "logger1", flogger);
  getFineGrainLogContext().initFineGrainLogger("envoy/src/foo.cc", "logger2", flogger);

  SpdLoggerSharedPtr p1 = getFineGrainLogContext().getFineGrainLogEntry(key1);
  SpdLoggerSharedPtr p2 = getFineGrainLogContext().getFineGrainLogEntry(key2);
  SpdLoggerSharedPtr p3 = getFineGrainLogContext().getFineGrainLogEntry(key3);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p2, nullptr);
  ASSERT_NE(p3, nullptr);
  ASSERT_EQ(p1->level(), spdlog::level::info);
  ASSERT_EQ(p2->level(), spdlog::level::info);
  ASSERT_EQ(p3->level(), spdlog::level::info);

  getFineGrainLogContext().updateVerbositySetting(
      {{"logger1", static_cast<int>(spdlog::level::warn)}});

  EXPECT_EQ(p1->level(), spdlog::level::warn);
  EXPECT_EQ(p2->level(), spdlog::level::warn);
  EXPECT_EQ(p3->level(), spdlog::level::info);
}

TEST(FineGrainLog, updateWithFileGroupAndGroupOnly) {
  Logger::Context::enableFineGrainLogger();
  getFineGrainLogContext().updateVerbositySetting({});
  getFineGrainLogContext().updateVerbosityDefaultLevel(spdlog::level::info);

  const std::string key1 = "source/common/http/http.cc:misc";
  const std::string key2 = "source/common/router/foo.cc:http";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger("source/common/http/http.cc", "misc", flogger);
  getFineGrainLogContext().initFineGrainLogger("source/common/router/foo.cc", "http", flogger);

  // Set "http" to TRACE using the match_group_only=true flag.
  getFineGrainLogContext().updateVerbositySetting(
      {{"http", static_cast<int>(spdlog::level::trace), true}});

  // key1 (misc group) should match "http" because its file basename is "http" IF
  // match_group_only=false. But here it should NOT match because match_group_only=true and its
  // group is "misc".
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(key1)->level(), spdlog::level::info);

  // key2 (http group) should match "http" because its group name is "http".
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(key2)->level(), spdlog::level::trace);

  // Now set "http" to WARN using the match_group_only=false flag (standard 'paths' behavior).
  getFineGrainLogContext().updateVerbositySetting(
      {{"http", static_cast<int>(spdlog::level::warn), false}});

  // BOTH should match now.
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(key1)->level(), spdlog::level::warn);
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(key2)->level(), spdlog::level::warn);
}

TEST(FineGrainLog, updateWithMultipleColons) {
  Logger::Context::enableFineGrainLogger();
  // Simulate a case where the file path might contain a colon (e.g. some generated code or Windows
  // path). On Linux, the last colon is always the separator.
  const std::string key = "path/with:colon/file.cc:group";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger("path/with:colon/file.cc", "group", flogger);

  // Match by group name.
  getFineGrainLogContext().updateVerbositySetting(
      {{"group", static_cast<int>(spdlog::level::warn), true}});
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(key)->level(), spdlog::level::warn);

  // Match by file path (the part before the last colon).
  getFineGrainLogContext().updateVerbositySetting(
      {{"path/with:colon/file.cc", static_cast<int>(spdlog::level::err)}});
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry(key)->level(), spdlog::level::err);
}

TEST(FineGrainLog, listFineGrainLoggersExcludesGroups) {
  Logger::Context::enableFineGrainLogger();
  const std::string file_key = "source/common/http/http.cc";
  const std::string group_key = "source/common/http/http.cc:http_group";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger("source/common/http/http.cc", "", flogger);
  getFineGrainLogContext().initFineGrainLogger("source/common/http/http.cc", "http_group", flogger);

  std::string loggers = getFineGrainLogContext().listFineGrainLoggers();
  EXPECT_THAT(loggers, HasSubstr(file_key));
  EXPECT_THAT(loggers, testing::Not(HasSubstr(group_key)));
}

TEST(FineGrainLog, getFineGrainLogEntryForFlush) {
  Logger::Context::enableFineGrainLogger();

  // Initialize logger for this file
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger(__FILE__, "", flogger);

  // Test with empty name
  SpdLoggerSharedPtr p1 = getFineGrainLogContext().getFineGrainLogEntryForFlush(__FILE__, "");
  EXPECT_NE(p1, nullptr);
  EXPECT_EQ(p1->name(), __FILE__);

  // Test with group name
  SpdLoggerSharedPtr p2 =
      getFineGrainLogContext().getFineGrainLogEntryForFlush(__FILE__, "test_group");
  EXPECT_EQ(p2, nullptr);

  // Now create it and check again
  std::atomic<spdlog::logger*> flogger2{nullptr};
  getFineGrainLogContext().initFineGrainLogger(__FILE__, "test_group", flogger2);
  p2 = getFineGrainLogContext().getFineGrainLogEntryForFlush(__FILE__, "test_group");
  EXPECT_NE(p2, nullptr);
  EXPECT_EQ(p2->name(), absl::StrCat(__FILE__, ":test_group"));
}

TEST(FineGrainLog, removeFineGrainLogEntryForTest) {
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger("to_be_removed", "", flogger);
  EXPECT_NE(getFineGrainLogContext().getFineGrainLogEntry("to_be_removed"), nullptr);

  getFineGrainLogContext().removeFineGrainLogEntryForTest("to_be_removed");
  EXPECT_EQ(getFineGrainLogContext().getFineGrainLogEntry("to_be_removed"), nullptr);
}

TEST(FineGrainLog, setAllFineGrainLoggers) {
  getFineGrainLogContext().setAllFineGrainLoggers(spdlog::level::warn);
  std::string list = getFineGrainLogContext().listFineGrainLoggers();
  EXPECT_THAT(list, HasSubstr("warn"));

  FineGrainLogLevelMap levels = getFineGrainLogContext().getAllFineGrainLogLevelsForTest();
  EXPECT_FALSE(levels.empty());
}

TEST(FineGrainLog, safeFileNameMatchTricky) {
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("", ""));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("", "a"));
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("*", ""));
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("**", ""));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("a", ""));
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("a*", "a"));
  EXPECT_TRUE(FineGrainLogContext::safeFileNameMatch("*a", "a"));
  EXPECT_FALSE(FineGrainLogContext::safeFileNameMatch("*a", "b"));
}

TEST(FineGrainLog, updateVerbositySettingInvalidLevelAndOptimization) {
  // Test invalid level
  getFineGrainLogContext().updateVerbositySetting({{"pattern", 10}});

  // Test optimization in appendVerbosityLogUpdate
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFineGrainLogContext().initFineGrainLogger("any", "", flogger);
  getFineGrainLogContext().updateVerbositySetting({{"*", 1}, {"subpattern", 2}});

  FineGrainLogLevelMap levels = getFineGrainLogContext().getAllFineGrainLogLevelsForTest();
  EXPECT_EQ(levels["any"], spdlog::level::debug); // level 1 is debug
}

} // namespace Envoy
