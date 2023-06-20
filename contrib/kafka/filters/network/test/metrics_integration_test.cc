#include "test/common/stats/stat_test_utility.h"

#include "contrib/kafka/filters/network/source/external/request_metrics.h"
#include "contrib/kafka/filters/network/source/external/response_metrics.h"
#include "contrib/kafka/filters/network/test/message_utilities.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace MetricsIntegrationTest {

class MetricsIntegrationTest : public testing::Test {
protected:
  Stats::TestUtil::TestStore store_;
  Stats::Scope& scope_{*store_.rootScope()};
  RichRequestMetricsImpl request_metrics_{scope_, "prefix"};
  RichResponseMetricsImpl response_metrics_{scope_, "prefix"};
};

constexpr static int32_t UPDATE_COUNT = 42;

TEST_F(MetricsIntegrationTest, ShouldUpdateRequestMetrics) {
  for (const int16_t api_key : MessageUtilities::apiKeys()) {
    // given
    // when
    for (int i = 0; i < UPDATE_COUNT; ++i) {
      request_metrics_.onRequest(api_key);
    }

    // then
    Stats::Counter& counter = store_.counter(MessageUtilities::requestMetric(api_key));
    ASSERT_EQ(counter.value(), UPDATE_COUNT);
  };
}

TEST_F(MetricsIntegrationTest, ShouldHandleUnparseableRequest) {
  // given
  // when
  for (int i = 0; i < UPDATE_COUNT; ++i) {
    request_metrics_.onUnknownRequest();
  }

  // then
  ASSERT_EQ(store_.counter("kafka.prefix.request.unknown").value(), UPDATE_COUNT);
}

TEST_F(MetricsIntegrationTest, ShouldUpdateResponseMetrics) {
  for (const int16_t api_key : MessageUtilities::apiKeys()) {
    // given
    // when
    for (int i = 0; i < UPDATE_COUNT; ++i) {
      response_metrics_.onResponse(api_key, 0);
    }

    // then
    Stats::Counter& counter = store_.counter(MessageUtilities::responseMetric(api_key));
    ASSERT_EQ(counter.value(), UPDATE_COUNT);
  };
}

TEST_F(MetricsIntegrationTest, ShouldHandleUnparseableResponse) {
  // given
  // when
  for (int i = 0; i < UPDATE_COUNT; ++i) {
    response_metrics_.onUnknownResponse();
  }

  // then
  ASSERT_EQ(store_.counter("kafka.prefix.response.unknown").value(), UPDATE_COUNT);
}

} // namespace MetricsIntegrationTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
