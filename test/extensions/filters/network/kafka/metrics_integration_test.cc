#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/kafka/external/request_metrics.h"
#include "extensions/filters/network/kafka/external/response_metrics.h"

#include "test/extensions/filters/network/kafka/message_utilities.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace MetricsIntegrationTest {

class MetricsIntegrationTest : public testing::Test {
protected:
  Stats::IsolatedStoreImpl scope_;
  RichRequestMetricsImpl request_metrics_{scope_, "prefix"};
  RichResponseMetricsImpl response_metrics_{scope_, "prefix"};
};

constexpr static int32_t UPDATE_COUNT = 42;

TEST_F(MetricsIntegrationTest, ShouldUpdateRequestMetrics) {
  for (int16_t api_key = 0; api_key < MessageUtilities::apiKeys(); ++api_key) {
    // given
    // when
    for (int i = 0; i < UPDATE_COUNT; ++i) {
      request_metrics_.onRequest(api_key);
    }

    // then
    Stats::Counter& counter = scope_.counter(MessageUtilities::requestMetric(api_key));
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
  ASSERT_EQ(scope_.counter("kafka.prefix.request.unknown").value(), UPDATE_COUNT);
}

TEST_F(MetricsIntegrationTest, ShouldUpdateResponseMetrics) {
  for (int16_t api_key = 0; api_key < MessageUtilities::apiKeys(); ++api_key) {
    // given
    // when
    for (int i = 0; i < UPDATE_COUNT; ++i) {
      response_metrics_.onResponse(api_key, 0);
    }

    // then
    Stats::Counter& counter = scope_.counter(MessageUtilities::responseMetric(api_key));
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
  ASSERT_EQ(scope_.counter("kafka.prefix.response.unknown").value(), UPDATE_COUNT);
}

} // namespace MetricsIntegrationTest
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
