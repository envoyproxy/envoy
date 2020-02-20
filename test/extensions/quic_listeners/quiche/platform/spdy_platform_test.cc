#include <functional>
#include <string>

#include "extensions/quic_listeners/quiche/platform/flags_impl.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"
#include "quiche/spdy/platform/api/spdy_bug_tracker.h"
#include "quiche/spdy/platform/api/spdy_containers.h"
#include "quiche/spdy/platform/api/spdy_endianness_util.h"
#include "quiche/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "quiche/spdy/platform/api/spdy_flags.h"
#include "quiche/spdy/platform/api/spdy_logging.h"
#include "quiche/spdy/platform/api/spdy_test_helpers.h"

// Basic tests to validate functioning of the QUICHE spdy platform
// implementation. For platform APIs in which the implementation is a simple
// typedef/passthrough to a std:: or absl:: construct, the tests are kept
// minimal, and serve primarily to verify the APIs compile and link without
// issue.

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {
namespace {

TEST(SpdyPlatformTest, SpdyBugTracker) {
  EXPECT_DEBUG_DEATH(SPDY_BUG << "Here is a bug,", " bug");
  EXPECT_DEBUG_DEATH(SPDY_BUG_IF(true) << "There is a bug,", " bug");
  EXPECT_LOG_NOT_CONTAINS("error", "", SPDY_BUG_IF(false) << "A feature is not a bug.");

  EXPECT_EQ(true, FLAGS_spdy_always_log_bugs_for_tests);
}

TEST(SpdyPlatformTest, SpdyHashMap) {
  spdy::SpdyHashMap<std::string, int> hmap;
  hmap.insert({"foo", 2});
  EXPECT_EQ(2, hmap["foo"]);
}

TEST(SpdyPlatformTest, SpdyHashSet) {
  spdy::SpdyHashSet<std::string, spdy::SpdyHash<std::string>, std::equal_to<std::string>> hset(
      {"foo", "bar"});
  EXPECT_EQ(1, hset.count("bar"));
  EXPECT_EQ(0, hset.count("qux"));
}

TEST(SpdyPlatformTest, SpdyEndianness) {
  EXPECT_EQ(0x1234, spdy::SpdyNetToHost16(spdy::SpdyHostToNet16(0x1234)));
  EXPECT_EQ(0x12345678, spdy::SpdyNetToHost32(spdy::SpdyHostToNet32(0x12345678)));
}

TEST(SpdyPlatformTest, SpdyEstimateMemoryUsage) {
  std::string s = "foo";
  // Stubbed out to always return 0.
  EXPECT_EQ(0, spdy::SpdyEstimateMemoryUsage(s));
}

TEST(SpdyPlatformTest, SpdyLog) {
  // SPDY_LOG macros are defined to QUIC_LOG macros, which is tested in
  // QuicPlatformTest. Here we just make sure SPDY_LOG macros compile.
  SPDY_LOG(INFO) << "INFO log may not show up by default.";
  SPDY_LOG(ERROR) << "ERROR log should show up by default.";

  // VLOG is only emitted if INFO is enabled and verbosity level is high enough.
  SPDY_VLOG(1) << "VLOG(1)";

  SPDY_DLOG(INFO) << "DLOG(INFO)";
  SPDY_DLOG(ERROR) << "DLOG(ERROR)";

  SPDY_DLOG_IF(ERROR, true) << "DLOG_IF(ERROR, true)";
  SPDY_DLOG_IF(ERROR, false) << "DLOG_IF(ERROR, false)";

  SPDY_DVLOG(2) << "DVLOG(2)";

  SPDY_DVLOG_IF(3, true) << "DVLOG_IF(3, true)";
  SPDY_DVLOG_IF(4, false) << "DVLOG_IF(4, false)";
}

TEST(SpdyPlatformTest, SpdyString) {
  std::string s = "foo";
  EXPECT_EQ('o', s[1]);
}

TEST(SpdyPlatformTest, SpdyTestHelpers) {
  auto bug = [](const char* error_message) { SPDY_BUG << error_message; };

  EXPECT_SPDY_BUG(bug("bug one is expected"), "bug one");
  EXPECT_SPDY_BUG(bug("bug two is expected"), "bug two");
}

TEST(SpdyPlatformTest, SpdyFlags) {
  auto& flag_registry = quiche::FlagRegistry::GetInstance();
  flag_registry.ResetFlags();
  EXPECT_FALSE(GetSpdyReloadableFlag(spdy_testonly_default_false));
  EXPECT_FALSE(GetSpdyRestartFlag(spdy_testonly_default_false));

  flag_registry.FindFlag("spdy_reloadable_flag_spdy_testonly_default_false")
      ->SetValueFromString("true");
  EXPECT_TRUE(GetSpdyReloadableFlag(spdy_testonly_default_false));
  EXPECT_FALSE(GetSpdyRestartFlag(spdy_testonly_default_false));

  flag_registry.ResetFlags();
  flag_registry.FindFlag("spdy_restart_flag_spdy_testonly_default_false")
      ->SetValueFromString("yes");
  EXPECT_FALSE(GetSpdyReloadableFlag(spdy_testonly_default_false));
  EXPECT_TRUE(GetSpdyRestartFlag(spdy_testonly_default_false));
}

} // namespace
} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
