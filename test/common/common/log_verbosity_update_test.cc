#include <string>

#include "source/common/common/fancy_logger.h"
#include "source/common/common/logger.h"

#include "test/test_common/logging.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(Fancy, safeFileNameMatch) {
  EXPECT_TRUE(FancyContext::safeFileNameMatch("foo", "foo"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("foo", "bar"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("foo", "fo"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("foo", "foo1"));

  EXPECT_TRUE(FancyContext::safeFileNameMatch("bar/foo.cc", "bar/foo.cc"));
  EXPECT_TRUE(FancyContext::safeFileNameMatch("*ba*r/f*oo.c*c", "bar/foo.cc"));

  EXPECT_TRUE(FancyContext::safeFileNameMatch("?a?/*.cc", "bar/foo.cc"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("bar/*.cc", "barbaz/foo.cc"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("*/*.cc", "barbaz/foo.cc2"));
  EXPECT_TRUE(FancyContext::safeFileNameMatch("b?r/*", "bar/foo.cc2"));
  EXPECT_TRUE(FancyContext::safeFileNameMatch("b?r/*", "bar/"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("?a?/?", "bar/"));
  EXPECT_TRUE(FancyContext::safeFileNameMatch("??r/?", "bar/a"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("/*", "foo/bar.h"));
  EXPECT_TRUE(FancyContext::safeFileNameMatch("/*", "/bar.h"));
}

TEST(Fancy, updateCurrentFilePath) {
  Logger::Context::enableFancyLogger();
  getFancyContext().setFancyLogger(__FILE__, spdlog::level::info);
  FANCY_LOG(info, "Info: verbosityUpdateCurrentFile test begins.");
  FANCY_LOG(debug, "Debug: you shouldn't see this message!");

  absl::string_view file_path = __FILE__;
  file_path.remove_suffix(3);
  std::pair<absl::string_view, int> update = std::make_pair(file_path, 1);
  getFancyContext().updateVerbositySetting({update});

  FANCY_LOG(debug, "Debug: now level is debug");
  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(__FILE__);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
}

TEST(Fancy, updateBasename) {
  Logger::Context::enableFancyLogger();
  getFancyContext().setFancyLogger(__FILE__, spdlog::level::info);
  FANCY_LOG(info, "Info: verbosityUpdateCurrentFile test begins.");
  FANCY_LOG(debug, "Debug: you shouldn't see this message!");

  absl::string_view file_path = __FILE__;
  file_path.remove_suffix(3);
  const size_t position = file_path.rfind('/');
  file_path.remove_prefix(position + 1);

  std::pair<absl::string_view, int> update = std::make_pair(file_path, 1);
  getFancyContext().updateVerbositySetting({update});

  FANCY_LOG(debug, "Debug: now level is debug");
  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(__FILE__);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
}

TEST(Fancy, multipleUpdatesBasename) {
  const char* file_1 = "envoy/src/foo.cc";
  const char* file_2 = "envoy/src/bar.cc";
  std::atomic<spdlog::logger*> flogger{0};
  getFancyContext().initFancyLogger(file_1, flogger);
  getFancyContext().initFancyLogger(file_2, flogger);

  std::pair<absl::string_view, int> update_1 = std::make_pair("foo", 1);
  std::pair<absl::string_view, int> update_2 = std::make_pair("bar", 3);
  getFancyContext().updateVerbositySetting({update_1, update_2});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(Fancy, multipleMatchesBasename) {
  const char* file_1 = "envoy/src/a.cc";
  const char* file_2 = "envoy/a.cc";
  std::atomic<spdlog::logger*> flogger{0};
  getFancyContext().initFancyLogger(file_1, flogger);
  getFancyContext().initFancyLogger(file_2, flogger);

  std::pair<absl::string_view, int> update_1 = std::make_pair("a", 3);
  getFancyContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(Fancy, globStarUpdate) {
  const char* file_1 = "envoy/src/foo.cc";
  const char* file_2 = "envoy/src/bar.cc";
  const char* file_3 = "envoy/baz.cc";
  std::atomic<spdlog::logger*> flogger{0};
  getFancyContext().initFancyLogger(file_1, flogger);
  getFancyContext().initFancyLogger(file_2, flogger);
  getFancyContext().initFancyLogger(file_3, flogger);

  std::pair<absl::string_view, int> update_1 = std::make_pair("envoy/*", 3);
  getFancyContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);

  std::pair<absl::string_view, int> update_2 = std::make_pair("envoy/src/*", 2);
  getFancyContext().updateVerbositySetting({update_2});

  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());

  std::pair<absl::string_view, int> update_3 = std::make_pair("*/src/*", 4);
  getFancyContext().updateVerbositySetting({update_3});

  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::err);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::err);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());

  std::pair<absl::string_view, int> update_4 = std::make_pair("*", 2);
  getFancyContext().updateVerbositySetting({update_4});

  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), spdlog::level::info);

  std::pair<absl::string_view, int> update_5 = std::make_pair("/*", 1);
  getFancyContext().updateVerbositySetting({update_5});

  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
}

TEST(Fancy, globQuestionUpdate) {
  const char* file_1 = "envoy/src/foo.cc";
  const char* file_2 = "envoy/f__.cc";
  const char* file_3 = "/bar.cc";
  std::atomic<spdlog::logger*> flogger{0};
  getFancyContext().initFancyLogger(file_1, flogger);
  getFancyContext().initFancyLogger(file_2, flogger);
  getFancyContext().initFancyLogger(file_3, flogger);

  std::pair<absl::string_view, int> update_1 = std::make_pair("f??", 3);
  getFancyContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());

  std::pair<absl::string_view, int> update_2 = std::make_pair("????y/*", 2);
  getFancyContext().updateVerbositySetting({update_2});
  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
}

TEST(Fancy, inOrderUpdate) {
  const char* file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{0};
  getFancyContext().initFancyLogger(file_1, flogger);

  std::pair<absl::string_view, int> update_1 = std::make_pair("f??", 3);
  std::pair<absl::string_view, int> update_2 = std::make_pair("envoy/*/f??", 2);
  getFancyContext().updateVerbositySetting({update_1, update_2});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(Fancy, invalidLevelUpdate) {
  const char* file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{0};
  getFancyContext().initFancyLogger(file_1, flogger);

  std::pair<absl::string_view, int> update_1 = std::make_pair("f??", 9);
  getFancyContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
}

} // namespace Envoy
