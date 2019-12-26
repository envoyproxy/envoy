#include "test/extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter_integration_test.h"

#include "common/http/header_map_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

void AdaptiveConcurrencyIntegrationTest::sendRequests(uint32_t request_count,
                                                      uint32_t num_forwarded) {
  ASSERT_LE(num_forwarded, request_count);

  // TODO (tonya11en):
  // We send header-only requests below because the adaptive concurrency filter will reject requests
  // when decoding their headers. If we try to send data, there's no way to ensure that the filter
  // doesn't respond between the client sending headers and data, invalidating the client's encoder
  // stream. We should change this integration test to allow for the ability to test this scenario.

  // We expect these requests to reach the upstream.
  for (uint32_t idx = 0; idx < num_forwarded; ++idx) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    responses_.push_back(std::move(response));
    upstream_connections_.emplace_back();
    upstream_requests_.emplace_back();

    ASSERT_TRUE(
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, upstream_connections_.back()));
    ASSERT_TRUE(
        upstream_connections_.back()->waitForNewStream(*dispatcher_, upstream_requests_.back()));

    ASSERT_TRUE(upstream_requests_.back()->waitForEndStream(*dispatcher_));
  }

  // These requests should be blocked by the filter, so they never make it to the upstream.
  auto blocked_counter = test_server_->counter(REQUEST_BLOCK_COUNTER_NAME)->value();
  for (uint32_t idx = 0; idx < request_count - num_forwarded; ++idx) {
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    responses_.push_back(std::move(response));

    test_server_->waitForCounterEq(REQUEST_BLOCK_COUNTER_NAME, ++blocked_counter);

    // These will remain nullptr.
    upstream_connections_.emplace_back();
    upstream_requests_.emplace_back();
  }

  ASSERT_EQ(upstream_connections_.size(), upstream_requests_.size());
  ASSERT_EQ(responses_.size(), upstream_requests_.size());
}

void AdaptiveConcurrencyIntegrationTest::respondToAllRequests(uint32_t forwarded_count,
                                                              std::chrono::milliseconds latency) {
  ASSERT_GE(responses_.size(), static_cast<size_t>(forwarded_count));

  timeSystem().sleep(latency);

  for (uint32_t idx = 0; idx < forwarded_count; ++idx) {
    respondToRequest(true);
  }

  while (!responses_.empty()) {
    respondToRequest(false);
  }
}

void AdaptiveConcurrencyIntegrationTest::respondToRequest(bool expect_forwarded) {
  ASSERT_EQ(upstream_connections_.size(), upstream_requests_.size());
  ASSERT_EQ(responses_.size(), upstream_requests_.size());

  if (expect_forwarded) {
    ASSERT_NE(upstream_connections_.front(), nullptr);
    ASSERT_NE(upstream_requests_.front(), nullptr);
    ASSERT_TRUE(upstream_requests_.front()->waitForEndStream(*dispatcher_));
    upstream_requests_.front()->encodeHeaders(default_response_headers_, false);
    upstream_requests_.front()->encodeData(1, true);
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
}

INSTANTIATE_TEST_SUITE_P(IpVersions, AdaptiveConcurrencyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/**
 * Test a single request returns successfully.
 */
TEST_P(AdaptiveConcurrencyIntegrationTest, TestConcurrency1) {
  customInit();

  EXPECT_EQ(0, test_server_->counter(REQUEST_BLOCK_COUNTER_NAME)->value());
  sendRequests(2, 1);
  respondToAllRequests(1, std::chrono::milliseconds(5));
  test_server_->waitForCounterEq(REQUEST_BLOCK_COUNTER_NAME, 1);
}

/**
 * Test many requests, where only a single request returns 200 during the minRTT window.
 */
TEST_P(AdaptiveConcurrencyIntegrationTest, TestManyConcurrency1) {
  customInit();

  EXPECT_EQ(0, test_server_->counter(REQUEST_BLOCK_COUNTER_NAME)->value());
  sendRequests(10, 1);
  respondToAllRequests(1, std::chrono::milliseconds(5));
  test_server_->waitForCounterEq(REQUEST_BLOCK_COUNTER_NAME, 9);
}

/**
 * TODO: Test the ability to increase/decrease the concurrency limit with request latencies based on
 * the minRTT value.
 *
 * See PR #8405.
 *
 * Previous attempts at this test took a long time when using simulated time, which resulted in
 * intermittent timeouts in CI.
 */

/**
 * TODO: Test the ability to enforce the concurrency limit outside of the minRTT calculation window.
 *
 * See PR #8405.
 *
 * Previous attempts at this test would hang during waitForHttpConnection after successfully sending
 * several requests to inflate the minRTT value. Alternative approaches that circumvented the need
 * for manually waiting included:
 *
 *   - Using a fault filter to inject delay into requests after passing the adaptive concurrency
 *   filter. This fails when using simulated time due to the fault filter's delay mechanism not
 *   being governed by the simulated time class. This required usage of real time, which sacrificed
 *   determinism.
 *
 *   - Buffering requests at the fake upstream and releasing them manually. Buffering via simulated
 *   time and releasing by advancing time does not work due to the only_one_thread.h assertions
 *   requiring simulated time to advance on a single thread. Buffering via a request queue and
 *   changes to the fake upstream requires too many changes to the fake upstream to be worth the
 *   investment of time, since it would be more worthwhile to overhaul the integration test
 *   framework to be event-driven rather than waitFor* driven.
 */

} // namespace Envoy
