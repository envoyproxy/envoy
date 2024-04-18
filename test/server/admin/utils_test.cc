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

  Http::Utility::QueryParamsMulti query_;
};

// Most utils paths are covered through other tests, these tests take of
// of special cases to get remaining coverage.
TEST_F(UtilsTest, HistogramMode) {
  Utility::HistogramBucketsMode histogram_buckets_mode = Utility::HistogramBucketsMode::Cumulative;
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::Unset, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "none");
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::Summary, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "summary");
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::Summary, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "cumulative");
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::Cumulative, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "disjoint");
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::Disjoint, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "detailed");
  EXPECT_TRUE(Utility::histogramBucketsParam(query_, histogram_buckets_mode).ok());
  EXPECT_EQ(Utility::HistogramBucketsMode::Detailed, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "garbage");
  absl::Status status = Utility::histogramBucketsParam(query_, histogram_buckets_mode);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.ToString(),
              HasSubstr("usage: /stats?histogram_buckets=(cumulative|disjoint|detailed|summary)"));
}

TEST_F(UtilsTest, QueryParam) {
  EXPECT_FALSE(query_.getFirstValue("key").has_value());
  query_.overwrite("key", "value");
  EXPECT_TRUE(query_.getFirstValue("key").has_value());
  EXPECT_EQ("value", query_.getFirstValue("key").value());
}

TEST_F(UtilsTest, NonEmptyQueryParam) {
  EXPECT_FALSE(Utility::nonEmptyQueryParam(query_, "key").has_value());
  query_.overwrite("key", "");
  EXPECT_FALSE(Utility::nonEmptyQueryParam(query_, "key").has_value());
  query_.overwrite("key", "value");
  EXPECT_TRUE(Utility::nonEmptyQueryParam(query_, "key").has_value());
  EXPECT_EQ("value", Utility::nonEmptyQueryParam(query_, "key").value());
}

} // namespace Server
} // namespace Envoy
