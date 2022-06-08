#include <string>

#include "source/common/common/fancy_logger.h"
#include "source/common/common/logger.h"

#include "test/test_common/logging.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {

TEST(Fancy, safeFileNameMatch) {
  // Exact file name or path match.
  EXPECT_TRUE(FancyContext::safeFileNameMatch("foo", "foo"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("foo", "bar"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("foo", "fo"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("foo", "foo1"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("bar/baz.cc", "bar/foo.cc"));

  // Star wildcard match.
  EXPECT_TRUE(FancyContext::safeFileNameMatch("*ba*r/f*oo.c*c", "bar/foo.cc"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("*/*.cc", "barbaz/foo.cc2"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("bar/*.cc", "barbaz/foo.cc"));
  EXPECT_FALSE(FancyContext::safeFileNameMatch("/*", "foo/bar.h"));
  EXPECT_TRUE(FancyContext::safeFileNameMatch("/*", "/bar.h"));

  // Question wildcard match.
  EXPECT_FALSE(FancyContext::safeFileNameMatch("?a?/?", "bar/"));
  EXPECT_TRUE(FancyContext::safeFileNameMatch("??r/?", "bar/a"));

  // Question and star wildcards match.
  EXPECT_TRUE(FancyContext::safeFileNameMatch("b?r/*", "bar/foo.cc2"));
  EXPECT_TRUE(FancyContext::safeFileNameMatch("b?r/*", "bar/"));
  EXPECT_TRUE(FancyContext::safeFileNameMatch("?a?/*.cc", "bar/foo.cc"));
}

TEST(Fancy, updateCurrentFilePath) {
  Logger::Context::enableFancyLogger();
  getFancyContext().setFancyLogger(__FILE__, spdlog::level::info);
  FANCY_LOG(info, "Info: verbosityUpdateCurrentFile test begins.");
  FANCY_LOG(debug, "Debug: you shouldn't see this message!");

  absl::string_view file_path = __FILE__;
  file_path.remove_suffix(3);
  const std::pair<absl::string_view, int> update =
      std::make_pair(file_path, static_cast<int>(spdlog::level::debug));
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

  const std::pair<absl::string_view, int> update =
      std::make_pair(file_path, static_cast<int>(spdlog::level::debug));
  getFancyContext().updateVerbositySetting({update});

  FANCY_LOG(debug, "Debug: now level is debug");
  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(__FILE__);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
}

TEST(Fancy, multipleUpdatesBasename) {
  const std::string file_1 = "envoy/src/foo.cc";
  const std::string file_2 = "envoy/src/bar.cc";
  static std::atomic<spdlog::logger*> flogger{nullptr};
  getFancyContext().initFancyLogger(file_1, flogger);
  getFancyContext().initFancyLogger(file_2, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("foo", static_cast<int>(spdlog::level::debug));
  const std::pair<absl::string_view, int> update_2 =
      std::make_pair("bar", static_cast<int>(spdlog::level::warn));
  getFancyContext().updateVerbositySetting({update_1, update_2});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::debug);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(Fancy, multipleMatchesBasename) {
  const std::string file_1 = "envoy/src/a.cc";
  const std::string file_2 = "envoy/a.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFancyContext().initFancyLogger(file_1, flogger);
  getFancyContext().initFancyLogger(file_2, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("a", static_cast<int>(spdlog::level::warn));
  getFancyContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(Fancy, globStarUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  const std::string file_2 = "envoy/src/bar.cc";
  const std::string file_3 = "envoy/baz.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFancyContext().initFancyLogger(file_1, flogger);
  getFancyContext().initFancyLogger(file_2, flogger);
  getFancyContext().initFancyLogger(file_3, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("envoy/*", static_cast<int>(spdlog::level::warn));
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

  const std::pair<absl::string_view, int> update_2 =
      std::make_pair("envoy/src/*", static_cast<int>(spdlog::level::info));
  getFancyContext().updateVerbositySetting({update_2});

  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());

  const std::pair<absl::string_view, int> update_3 =
      std::make_pair("*/src/*", static_cast<int>(spdlog::level::err));
  getFancyContext().updateVerbositySetting({update_3});

  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::err);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::err);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());

  const std::pair<absl::string_view, int> update_4 =
      std::make_pair("*", static_cast<int>(spdlog::level::info));
  getFancyContext().updateVerbositySetting({update_4});

  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), spdlog::level::info);

  const std::pair<absl::string_view, int> update_5 =
      std::make_pair("/*", static_cast<int>(spdlog::level::debug));
  getFancyContext().updateVerbositySetting({update_5});

  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
}

TEST(Fancy, globQuestionUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  const std::string file_2 = "envoy/f__.cc";
  const std::string file_3 = "/bar.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFancyContext().initFancyLogger(file_1, flogger);
  getFancyContext().initFancyLogger(file_2, flogger);
  getFancyContext().initFancyLogger(file_3, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("f??", static_cast<int>(spdlog::level::warn));
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

  const std::pair<absl::string_view, int> update_2 =
      std::make_pair("????y/*", static_cast<int>(spdlog::level::info));
  getFancyContext().updateVerbositySetting({update_2});
  p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_2);
  EXPECT_EQ(p->level(), spdlog::level::info);
  p = getFancyContext().getFancyLogEntry(file_3);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
}

TEST(Fancy, inOrderUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFancyContext().initFancyLogger(file_1, flogger);

  const std::pair<absl::string_view, int> update_1 =
      std::make_pair("f??", static_cast<int>(spdlog::level::warn));
  const std::pair<absl::string_view, int> update_2 =
      std::make_pair("envoy/*/f??", static_cast<int>(spdlog::level::info));
  getFancyContext().updateVerbositySetting({update_1, update_2});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), spdlog::level::warn);
}

TEST(Fancy, invalidLevelUpdate) {
  const std::string file_1 = "envoy/src/foo.cc";
  std::atomic<spdlog::logger*> flogger{nullptr};
  getFancyContext().initFancyLogger(file_1, flogger);

  const std::pair<absl::string_view, int> update_1 = std::make_pair("f??", 9);
  getFancyContext().updateVerbositySetting({update_1});

  SpdLoggerSharedPtr p = getFancyContext().getFancyLogEntry(file_1);
  EXPECT_NE(p, nullptr);
  EXPECT_EQ(p->level(), Logger::Context::getFancyDefaultLevel());
}

} // namespace Envoy
