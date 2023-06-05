#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/stats_params.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

TEST(StatsParamsTest, TypeToString) {
  EXPECT_EQ("TextReadouts", StatsParams::typeToString(StatsType::TextReadouts));
  EXPECT_EQ("Gauges", StatsParams::typeToString(StatsType::Gauges));
  EXPECT_EQ("Counters", StatsParams::typeToString(StatsType::Counters));
  EXPECT_EQ("Histograms", StatsParams::typeToString(StatsType::Histograms));
  EXPECT_EQ("All", StatsParams::typeToString(StatsType::All));
}

TEST(StatsParamsTest, ParseParamsType) {
  Buffer::OwnedImpl response;
  StatsParams params;

  ASSERT_EQ(Http::Code::OK, params.parse("?type=TextReadouts", response));
  EXPECT_EQ(StatsType::TextReadouts, params.type_);
  ASSERT_EQ(Http::Code::OK, params.parse("?type=Gauges", response));
  EXPECT_EQ(StatsType::Gauges, params.type_);
  ASSERT_EQ(Http::Code::OK, params.parse("?type=Counters", response));
  EXPECT_EQ(StatsType::Counters, params.type_);
  ASSERT_EQ(Http::Code::OK, params.parse("?type=Histograms", response));
  EXPECT_EQ(StatsType::Histograms, params.type_);
  ASSERT_EQ(Http::Code::OK, params.parse("?type=All", response));
  EXPECT_EQ(StatsType::All, params.type_);
  EXPECT_EQ(Http::Code::BadRequest, params.parse("?type=bogus", response));
}

TEST(StatsParamsTest, ParseParamsFormat) {
  Buffer::OwnedImpl response;
  StatsParams params;

  ASSERT_EQ(Http::Code::OK, params.parse("?format=text", response));
  EXPECT_EQ(StatsFormat::Text, params.format_);
#ifdef ENVOY_ADMIN_HTML
  ASSERT_EQ(Http::Code::OK, params.parse("?format=html", response));
  EXPECT_EQ(StatsFormat::Html, params.format_);
  ASSERT_EQ(Http::Code::OK, params.parse("?format=active-html", response));
  EXPECT_EQ(StatsFormat::ActiveHtml, params.format_);
#else
  EXPECT_EQ(Http::Code::BadRequest, params.parse("?format=html", response));
  EXPECT_EQ(Http::Code::BadRequest, params.parse("?format=active-html", response));
#endif
  ASSERT_EQ(Http::Code::OK, params.parse("?format=json", response));
  EXPECT_EQ(StatsFormat::Json, params.format_);
  ASSERT_EQ(Http::Code::OK, params.parse("?format=prometheus", response));
  EXPECT_EQ(StatsFormat::Prometheus, params.format_);
  EXPECT_EQ(Http::Code::BadRequest, params.parse("?format=bogus", response));
}

TEST(StatsParamsTest, ParseParamsHidden) {
  Buffer::OwnedImpl response;
  StatsParams params;

  EXPECT_EQ(HiddenFlag::Exclude, params.hidden_);

  ASSERT_EQ(Http::Code::OK, params.parse("?hidden=include", response));
  EXPECT_EQ(HiddenFlag::Include, params.hidden_);
  ASSERT_EQ(Http::Code::OK, params.parse("?hidden=only", response));
  EXPECT_EQ(HiddenFlag::ShowOnly, params.hidden_);
  ASSERT_EQ(Http::Code::OK, params.parse("?hidden=exclude", response));
  EXPECT_EQ(HiddenFlag::Exclude, params.hidden_);
  ASSERT_EQ(Http::Code::BadRequest, params.parse("?hidden=foo", response));
  ASSERT_EQ(Http::Code::OK, params.parse("?hidden", response));
  EXPECT_EQ(HiddenFlag::Exclude, params.hidden_);
}

TEST(StatsParamsTest, ParseParamsFilter) {
  Buffer::OwnedImpl response;

  {
    StatsParams params;
    ASSERT_EQ(Http::Code::OK, params.parse("?filter=foo", response));
    EXPECT_NE(nullptr, params.re2_filter_);
  }
}

} // namespace Server
} // namespace Envoy
