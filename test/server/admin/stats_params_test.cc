#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/stats_params.h"

#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

TEST(StatsParamsTest, ParseParamsFormat) {
  Buffer::OwnedImpl response;
  StatsParams params;

  ASSERT_EQ(Http::Code::OK, params.parse("?format=text", response));
  EXPECT_EQ(StatsFormat::Text, params.format_);
  ASSERT_EQ(Http::Code::OK, params.parse("?format=json", response));
  EXPECT_EQ(StatsFormat::Json, params.format_);
  ASSERT_EQ(Http::Code::OK, params.parse("?format=prometheus", response));
  EXPECT_EQ(StatsFormat::Prometheus, params.format_);
  EXPECT_EQ(Http::Code::BadRequest, params.parse("?format=bogus", response));
}

TEST(StatsParamsTest, ParseParamsFilter) {
  Buffer::OwnedImpl response;

  {
    TestScopedRuntime scoped_runtime;
    scoped_runtime.mergeValues({{"envoy.reloadable_features.admin_stats_filter_use_re2", "false"}});

    StatsParams params;
    ASSERT_EQ(Http::Code::OK, params.parse("?filter=foo", response));
    EXPECT_NE(nullptr, params.filter_);
    EXPECT_EQ(nullptr, params.re2_filter_);
  }

  {
    StatsParams params;
    ASSERT_EQ(Http::Code::OK, params.parse("?filter=foo", response));
    EXPECT_EQ(nullptr, params.filter_);
    EXPECT_NE(nullptr, params.re2_filter_);
  }
}

} // namespace Server
} // namespace Envoy
