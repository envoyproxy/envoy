#include "test/extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter_integration_test.h"

#include "common/http/header_map_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

void AdaptiveConcurrencyIntegrationTest::sendRequests(const uint32_t request_count, const uint32_t num_forwarded) {
  ASSERT_LE(num_forwarded, request_count);

  // We expect these requests to reach the upstream.
  for (uint32_t idx = 0; idx < num_forwarded; ++idx) {
    auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
    responses_.push_back(std::move(encoder_decoder.second));
    upstream_connections_.emplace_back();
    upstream_requests_.emplace_back();

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, upstream_connections_.back()));
    ASSERT_TRUE(upstream_connections_.back()->waitForNewStream(*dispatcher_, upstream_requests_.back()));

    codec_client_->sendData(encoder_decoder.first, 0, true);
    ASSERT_TRUE(upstream_requests_.back()->waitForEndStream(*dispatcher_));
  }

  for (uint32_t idx = 0; idx < request_count - num_forwarded; ++idx) {
    auto encoder_decoder = codec_client_->startRequest(default_request_headers_);
    responses_.push_back(std::move(encoder_decoder.second));
    codec_client_->sendData(encoder_decoder.first, 0, true);

    // These will remain nullptr.
    upstream_connections_.emplace_back();
    upstream_requests_.emplace_back();
  }

  ASSERT_EQ(upstream_connections_.size(), upstream_requests_.size());
  ASSERT_EQ(responses_.size(), upstream_requests_.size());
}

void AdaptiveConcurrencyIntegrationTest::respondToAllRequests(const int forwarded_count) {
  ASSERT_GE(responses_.size(), static_cast<size_t>(forwarded_count));

  for (int idx = 0; idx < forwarded_count; ++idx) {
    respondToRequest(true);
  }
  while (!responses_.empty()) {
    respondToRequest(false);
  }
}

void AdaptiveConcurrencyIntegrationTest::respondToRequest(const bool expect_forwarded) {
  ASSERT_EQ(upstream_connections_.size(), upstream_requests_.size());
  ASSERT_EQ(responses_.size(), upstream_requests_.size());

  if (expect_forwarded) {
    ASSERT_NE(upstream_connections_.front(), nullptr);
    ASSERT_NE(upstream_requests_.front(), nullptr);
    ASSERT_TRUE(upstream_requests_.front()->waitForEndStream(*dispatcher_));
    upstream_requests_.front()->encodeHeaders(default_response_headers_, true);
  }

  responses_.front()->waitForEndStream();

  if (expect_forwarded) {
    EXPECT_TRUE(upstream_requests_.front()->complete());
  }

  EXPECT_TRUE(responses_.front()->complete());

  if (expect_forwarded) {
    verifyResponseForwarded(std::move(responses_.front()));
    ASSERT_TRUE(upstream_connections_.front()->close());
    ASSERT_TRUE(upstream_connections_.front()->waitForDisconnect());
  } else {
    verifyResponseBlocked(std::move(responses_.front()));
  }
  
  upstream_connections_.pop_front();
  upstream_requests_.pop_front();
  responses_.pop_front();
  codec_client_->close();
}

uint32_t
AdaptiveConcurrencyIntegrationTest::inflateConcurrencyLimit(const uint64_t limit_lower_bound) {
  // Send requests until the gauge exists.
  while (!test_server_->gauge(CONCURRENCY_LIMIT_GAUGE_NAME)) {
    sendRequests(1, 1);
    respondToAllRequests(1);
  }

  while (test_server_->gauge(CONCURRENCY_LIMIT_GAUGE_NAME)->value() < limit_lower_bound) {
    const auto min_rtt = test_server_->gauge(MIN_RTT_GAUGE_NAME)->value();
    sendRequests(1, 1);
    // Choosing a value less than the minRTT.
    timeSystem().sleep(std::chrono::milliseconds(min_rtt) / 2);
    respondToAllRequests(1);
  }
  return test_server_->gauge(CONCURRENCY_LIMIT_GAUGE_NAME)->value();
}

void AdaptiveConcurrencyIntegrationTest::deflateConcurrencyLimit(const uint64_t limit_upper_bound) {
  ASSERT_GT(limit_upper_bound, 1);

  // Send requests until the gauge exists.
  while (!test_server_->gauge(CONCURRENCY_LIMIT_GAUGE_NAME)) {
    sendRequests(1, 1);
    timeSystem().sleep(std::chrono::milliseconds(1));
    respondToAllRequests(1);
  }

  // We cannot break when the concurrency limit is 1, because this implies we're in a minRTT
  // recalculation window. This is not a decrease in the concurrency limit due to latency samples.
  while (test_server_->gauge(CONCURRENCY_LIMIT_GAUGE_NAME)->value() != 1 &&
         test_server_->gauge(CONCURRENCY_LIMIT_GAUGE_NAME)->value() >= limit_upper_bound) {
    const auto min_rtt = test_server_->gauge(MIN_RTT_GAUGE_NAME)->value();
    sendRequests(1, 1);
    timeSystem().sleep(std::chrono::milliseconds(min_rtt * 2));
    respondToAllRequests(1);
  }
}

INSTANTIATE_TEST_SUITE_P(IpVersions, AdaptiveConcurrencyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/**
 * Test a single request returns successfully.
 */
TEST_P(AdaptiveConcurrencyIntegrationTest, TestConcurrency1) {
  customInit();

  sendRequests(2, 1);
  respondToAllRequests(1);
  test_server_->waitForCounterGe(REQUEST_BLOCK_COUNTER_NAME, 1);
}

/**
 * Test many requests, where only a single request returns 200 during the minRTT window.
 */
TEST_P(AdaptiveConcurrencyIntegrationTest, TestManyConcurrency1) {
  customInit();

  sendRequests(10, 1);
  respondToAllRequests(1);
  test_server_->waitForCounterGe(REQUEST_BLOCK_COUNTER_NAME, 9);
}

/**
 * Test the ability to increase/decrease the concurrency limit with request latencies based on the
 * minRTT value.
 */
TEST_P(AdaptiveConcurrencyIntegrationTest, TestConcurrencyLimitMovement) {
  customInit();

  // Cause the concurrency limit to oscillate.
  for (int idx = 0; idx < 3; ++idx) {
    inflateConcurrencyLimit(100);
    deflateConcurrencyLimit(10);
  }
}

} // namespace Envoy
