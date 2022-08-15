#include "envoy/http/query_params.h"

#include "source/server/admin/utils.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {
namespace Server {

class UtilsTest : public testing::Test {
protected:
  UtilsTest() = default;

  Http::Utility::QueryParams query_;
};

// Most utils paths are covered through other tests, these tests take of
// of special cases to get remaining coverage.
TEST_F(UtilsTest, BadServerState) {
  Utility::serverState(Init::Manager::State::Uninitialized, true);
  EXPECT_ENVOY_BUG(Utility::serverState(static_cast<Init::Manager::State>(123), true),
                   "unexpected server state");
}

TEST_F(UtilsTest, HistogramMode) {
  Utility::HistogramBucketsMode histogram_buckets_mode = Utility::HistogramBucketsMode::Cumulative;
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::NoBuckets, histogram_buckets_mode);
  query_["histogram_buckets"] = "none";
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::NoBuckets, histogram_buckets_mode);
  query_["histogram_buckets"] = "cumulative";
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::Cumulative, histogram_buckets_mode);
  query_["histogram_buckets"] = "disjoint";
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::Disjoint, histogram_buckets_mode);
  query_["histogram_buckets"] = "garbage";
  absl::Status status = Utility::histogramBucketsParam(query_, histogram_buckets_mode);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.ToString(),
              HasSubstr("usage: /stats?histogram_buckets=(cumulative|disjoint|none)"));
}

TEST_F(UtilsTest, QueryParam) {
  EXPECT_FALSE(Utility::queryParam(query_, "key").has_value());
  query_["key"] = "";
  EXPECT_FALSE(Utility::queryParam(query_, "key").has_value());
  query_["key"] = "value";
  EXPECT_TRUE(Utility::queryParam(query_, "key").has_value());
  EXPECT_EQ("value", Utility::queryParam(query_, "key").value());
}

} // namespace Server
} // namespace Envoy
