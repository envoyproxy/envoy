#include <memory>

#include "test/test_common/logging.h"

#include "gtest/gtest.h"
#include "quiche/http2/platform/api/http2_arraysize.h"
#include "quiche/http2/platform/api/http2_bug_tracker.h"
#include "quiche/http2/platform/api/http2_containers.h"
#include "quiche/http2/platform/api/http2_estimate_memory_usage.h"
#include "quiche/http2/platform/api/http2_logging.h"
#include "quiche/http2/platform/api/http2_optional.h"
#include "quiche/http2/platform/api/http2_ptr_util.h"
#include "quiche/http2/platform/api/http2_string.h"
#include "quiche/http2/platform/api/http2_string_piece.h"

// Basic tests to validate functioning of the QUICHE http2 platform
// implementation. For platform APIs in which the implementation is a simple
// typedef/passthrough to a std:: or absl:: construct, the tests are kept
// minimal, and serve primarily to verify the APIs compile and link without
// issue.

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {
namespace {

TEST(Http2PlatformTest, Http2Arraysize) {
  int array[] = {0, 1, 2, 3, 4};
  EXPECT_EQ(5, HTTP2_ARRAYSIZE(array));
}

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
  http2::Http2String s = "foo";
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

TEST(Http2PlatformTest, Http2Optional) {
  http2::Http2Optional<int> opt;
  EXPECT_FALSE(opt.has_value());
  opt = 3;
  EXPECT_TRUE(opt.has_value());
}

TEST(Http2PlatformTest, Http2MakeUnique) {
  auto p = http2::Http2MakeUnique<int>(4);
  EXPECT_EQ(4, *p);
}

TEST(Http2PlatformTest, Http2String) {
  http2::Http2String s = "foo";
  EXPECT_EQ('o', s[1]);
}

TEST(Http2PlatformTest, Http2StringPiece) {
  http2::Http2String s = "bar";
  http2::Http2StringPiece sp(s);
  EXPECT_EQ('b', sp[0]);
}

} // namespace
} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
