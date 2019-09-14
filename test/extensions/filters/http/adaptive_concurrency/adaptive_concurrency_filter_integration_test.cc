#include "test/extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter_integration_test.h"

#include "common/http/header_map_impl.h"

#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

void AdaptiveConcurrencyIntegrationTest::sendRequests(
    const int request_count, const uint32_t delay_ms = kDefaultRequestDelayMs) {
  auto headers = default_request_headers_;
  headers.addCopy("x-envoy-fault-delay-request", std::to_string(delay_ms));
  for (int ii = 0; ii < request_count; ++ii) {
    auto encoder_decoder = codec_client_->startRequest(headers);
    response_q_.emplace(std::move(encoder_decoder.second));
    codec_client_->sendData(encoder_decoder.first, 0, true);
  }
}

void AdaptiveConcurrencyIntegrationTest::respondToAllRequests(const int num_forwarded) {
  int forwarded_count = 0;
  while (!response_q_.empty()) {
    auto response = std::move(response_q_.front());
    response_q_.pop();
    response->waitForEndStream();
    EXPECT_TRUE(response->complete());
    const auto status_code = response->headers().Status()->value().getStringView();
    if (status_code == "200") {
      ++forwarded_count;
      continue;
    }
    EXPECT_EQ("503", status_code);
  }

  EXPECT_EQ(num_forwarded, forwarded_count);
}

IntegrationStreamDecoderPtr AdaptiveConcurrencyIntegrationTest::respondToRequest() {
  auto response = std::move(response_q_.front());
  response_q_.pop();
  response->waitForEndStream();
  EXPECT_TRUE(response->complete());
  return response;
}

void AdaptiveConcurrencyIntegrationTest::inflateConcurrencyLimit(const uint64_t limit_lower_bound) {
  // Send requests until the gauge exists.
  while (!test_server_->gauge(kConcurrencyLimitGaugeName)) {
    sendRequests(1);
    respondToAllRequests(1);
  }

  while (test_server_->gauge(kConcurrencyLimitGaugeName)->value() < limit_lower_bound) {
    const auto min_rtt = test_server_->gauge(kMinRTTGaugeName)->value();
    sendRequests(1, min_rtt / 2);
    respondToAllRequests(1);
  }
}

void AdaptiveConcurrencyIntegrationTest::deflateConcurrencyLimit(const uint64_t limit_upper_bound) {
  ASSERT(limit_upper_bound > 1);
  // Send requests until the gauge exists.
  while (!test_server_->gauge(kConcurrencyLimitGaugeName)) {
    sendRequests(1);
    respondToAllRequests(1);
  }

  // We cannot break when the concurrency limit is 1, because this implies we're in a minRTT
  // recalculation window. This is not a decrease in the concurrency limit due to latency samples.
  while (test_server_->gauge(kConcurrencyLimitGaugeName)->value() != 1 &&
         test_server_->gauge(kConcurrencyLimitGaugeName)->value() >= limit_upper_bound) {
    const auto min_rtt = test_server_->gauge(kMinRTTGaugeName)->value();
    sendRequests(1, min_rtt * 2);
    respondToAllRequests(1);
  }
}

INSTANTIATE_TEST_SUITE_P(IpVersions, AdaptiveConcurrencyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(AdaptiveConcurrencyIntegrationTest, TestManyConcurrency1) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  sendRequests(10);
  respondToAllRequests(1);
  test_server_->waitForCounterGe(kRequestBlockCounterName, 9);
}

TEST_P(AdaptiveConcurrencyIntegrationTest, TestConcurrency1) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  sendRequests(2);
  auto response = respondToRequest();
  verifyResponseForwarded(std::move(response));
  response = respondToRequest();
  verifyResponseBlocked(std::move(response));
}

TEST_P(AdaptiveConcurrencyIntegrationTest, TestConcurrencyLimitMovement) {
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Cause the concurrency limit to oscillate.
  for (int ii = 0; ii < 3; ++ii) {
    inflateConcurrencyLimit(100);
    deflateConcurrencyLimit(10);
  }
}

} // namespace Envoy
