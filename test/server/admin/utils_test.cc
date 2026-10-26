#include "envoy/http/query_params.h"

#include "source/server/admin/utils.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::Envoy::StatusHelpers::HasStatus;
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
  EXPECT_OK(Utility::histogramBucketsParam(query_, histogram_buckets_mode));
  EXPECT_EQ(Utility::HistogramBucketsMode::Unset, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "none");
  EXPECT_OK(Utility::histogramBucketsParam(query_, histogram_buckets_mode));
  EXPECT_EQ(Utility::HistogramBucketsMode::Summary, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "summary");
  EXPECT_OK(Utility::histogramBucketsParam(query_, histogram_buckets_mode));
  EXPECT_EQ(Utility::HistogramBucketsMode::Summary, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "cumulative");
  EXPECT_OK(Utility::histogramBucketsParam(query_, histogram_buckets_mode));
  EXPECT_EQ(Utility::HistogramBucketsMode::Cumulative, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "disjoint");
  EXPECT_OK(Utility::histogramBucketsParam(query_, histogram_buckets_mode));
  EXPECT_EQ(Utility::HistogramBucketsMode::Disjoint, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "detailed");
  EXPECT_OK(Utility::histogramBucketsParam(query_, histogram_buckets_mode));
  EXPECT_EQ(Utility::HistogramBucketsMode::Detailed, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "prometheusnative");
  EXPECT_OK(Utility::histogramBucketsParam(query_, histogram_buckets_mode));
  EXPECT_EQ(Utility::HistogramBucketsMode::PrometheusNative, histogram_buckets_mode);
  query_.overwrite("histogram_buckets", "garbage");
  EXPECT_THAT(
      Utility::histogramBucketsParam(query_, histogram_buckets_mode),
      HasStatus(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "usage: "
              "/stats?histogram_buckets=(cumulative|disjoint|detailed|summary|prometheusnative)")));
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
