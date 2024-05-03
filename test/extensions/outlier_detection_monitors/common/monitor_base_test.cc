//#include "envoy/upstream/outlier_detection.h"

#include "source/extensions/outlier_detection_monitors/common/monitor_base_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

// Define type of Result used only for tests.
class ResultForTest : public TypedError<Upstream::Outlier::ErrorType::TEST> {};

TEST(MonitorBaseTest, HTTPCodeError) {
  HttpCode http(200);

  ASSERT_EQ(200, http.code());
  ASSERT_EQ(Upstream::Outlier::ErrorType::HTTP_CODE, http.type());
}

TEST(MonitorBaseTest, HTTPCodeErrorBucket) {
  // HttpCode http_200(200), http_403(403), http_500(500);

  HTTPErrorCodesBucket bucket("not-needed", 400, 404);
  ASSERT_TRUE(bucket.matchType(HttpCode(200)));
  ASSERT_FALSE(bucket.match(HttpCode(200)));
  ASSERT_FALSE(bucket.match(HttpCode(500)));
  ASSERT_TRUE(bucket.match(HttpCode(403)));
  // test at the edges of the bucket.
  ASSERT_TRUE(bucket.match(HttpCode(400)));
  ASSERT_TRUE(bucket.match(HttpCode(404)));

  // Http-codes bucket should not "catch" other types.
  ASSERT_FALSE(bucket.matchType(ResultForTest()));
}

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
