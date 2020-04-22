// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <memory>
#include <string>

#include "extensions/quic_listeners/quiche/platform/flags_impl.h"

#include "test/test_common/logging.h"

#include "gtest/gtest.h"
#include "quiche/http2/platform/api/http2_bug_tracker.h"
#include "quiche/http2/platform/api/http2_containers.h"
#include "quiche/http2/platform/api/http2_estimate_memory_usage.h"
#include "quiche/http2/platform/api/http2_flags.h"
#include "quiche/http2/platform/api/http2_logging.h"
#include "quiche/http2/platform/api/http2_macros.h"
#include "quiche/http2/test_tools/http2_random.h"

// Basic tests to validate functioning of the QUICHE http2 platform
// implementation. For platform APIs in which the implementation is a simple
// typedef/passthrough to a std:: or absl:: construct, the tests are kept
// minimal, and serve primarily to verify the APIs compile and link without
// issue.

namespace http2 {
namespace {

TEST(Http2PlatformTest, Http2BugTracker) {
  EXPECT_DEBUG_DEATH(HTTP2_BUG << "Here is a bug,", " bug");
  EXPECT_DEBUG_DEATH(HTTP2_BUG_IF(true) << "There is a bug,", " bug");
  EXPECT_LOG_NOT_CONTAINS("error", "", HTTP2_BUG_IF(false) << "A feature is not a bug.");

  EXPECT_EQ(true, FLAGS_http2_always_log_bugs_for_tests);
}

TEST(Http2PlatformTest, Http2Deque) {
  http2::Http2Deque<int> deque;
  deque.push_back(10);
  EXPECT_EQ(10, deque.back());
}

TEST(Http2PlatformTest, Http2EstimateMemoryUsage) {
  std::string s = "foo";
  // Stubbed out to always return 0.
  EXPECT_EQ(0, http2::Http2EstimateMemoryUsage(s));
}

TEST(Http2PlatformTest, Http2Log) {
  // HTTP2_LOG macros are defined to QUIC_LOG macros, which is tested in
  // QuicPlatformTest. Here we just make sure HTTP2_LOG macros compile.
  HTTP2_LOG(INFO) << "INFO log may not show up by default.";
  HTTP2_LOG(ERROR) << "ERROR log should show up by default.";

  // VLOG are only emitted if INFO is enabled and verbosity level is high enough.
  HTTP2_VLOG(1) << "VLOG(1)";

  HTTP2_DLOG(INFO) << "DLOG(INFO)";
  HTTP2_DLOG(ERROR) << "DLOG(ERROR)";

  HTTP2_DLOG_IF(ERROR, true) << "DLOG_IF(ERROR, true)";
  HTTP2_DLOG_IF(ERROR, false) << "DLOG_IF(ERROR, false)";

  HTTP2_DVLOG(2) << "DVLOG(2)";

  HTTP2_DVLOG_IF(3, true) << "DVLOG_IF(3, true)";
  HTTP2_DVLOG_IF(4, false) << "DVLOG_IF(4, false)";

  HTTP2_DLOG_EVERY_N(ERROR, 2) << "DLOG_EVERY_N(ERROR, 2)";
}

TEST(Http2PlatformTest, Http2StringPiece) {
  std::string s = "bar";
  quiche::QuicheStringPiece sp(s);
  EXPECT_EQ('b', sp[0]);
}

TEST(Http2PlatformTest, Http2Macro) {
  EXPECT_DEBUG_DEATH(HTTP2_UNREACHABLE(), "");
  EXPECT_DEATH(HTTP2_DIE_IF_NULL(nullptr), "");
}

TEST(Http2PlatformTest, Http2Flags) {
  auto& flag_registry = quiche::FlagRegistry::GetInstance();
  flag_registry.ResetFlags();
  EXPECT_FALSE(GetHttp2ReloadableFlag(http2_testonly_default_false));
  SetHttp2ReloadableFlag(http2_testonly_default_false, true);
  EXPECT_TRUE(GetHttp2ReloadableFlag(http2_testonly_default_false));

  for (std::string s : {"1", "t", "true", "TRUE", "y", "yes", "Yes"}) {
    SetHttp2ReloadableFlag(http2_testonly_default_false, false);
    EXPECT_FALSE(GetHttp2ReloadableFlag(http2_testonly_default_false));
    EXPECT_TRUE(flag_registry.FindFlag("http2_reloadable_flag_http2_testonly_default_false")
                    ->SetValueFromString(s));
    EXPECT_TRUE(GetHttp2ReloadableFlag(http2_testonly_default_false));
  }
  for (std::string s : {"0", "f", "false", "FALSE", "n", "no", "No"}) {
    SetHttp2ReloadableFlag(http2_testonly_default_false, true);
    EXPECT_TRUE(GetHttp2ReloadableFlag(http2_testonly_default_false));
    EXPECT_TRUE(flag_registry.FindFlag("http2_reloadable_flag_http2_testonly_default_false")
                    ->SetValueFromString(s));
    EXPECT_FALSE(GetHttp2ReloadableFlag(http2_testonly_default_false));
  }
  for (std::string s : {"some", "invalid", "values", ""}) {
    SetHttp2ReloadableFlag(http2_testonly_default_false, false);
    EXPECT_FALSE(GetHttp2ReloadableFlag(http2_testonly_default_false));
    EXPECT_FALSE(flag_registry.FindFlag("http2_reloadable_flag_http2_testonly_default_false")
                     ->SetValueFromString(s));
    EXPECT_FALSE(GetHttp2ReloadableFlag(http2_testonly_default_false));
  }
}

} // namespace
} // namespace http2
