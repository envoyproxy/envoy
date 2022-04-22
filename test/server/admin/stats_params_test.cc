#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/stats_params.h"

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

} // namespace Server
} // namespace Envoy
