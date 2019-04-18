#include "test/integration/http_timeout_integration_test.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_SUITE_P(IpVersions, HttpTimeoutIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Sends a request with a global timeout specified, sleeps for longer than the
// timeout, and ensures that a timeout is received.
TEST_P(HttpTimeoutIntegrationTest, GlobalTimeout) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"x-forwarded-for", "10.0.0.1"},
                              {"x-envoy-upstream-rq-timeout-ms", "500"}});
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  codec_client_->sendData(*request_encoder_, 0, true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger global timeout.
  timeSystem().sleep(std::chrono::milliseconds(501));

  // Ensure we got a timeout downstream and canceled the upstream request.
  response->waitForHeaders();
  ASSERT_TRUE(upstream_request_->waitForReset(std::chrono::milliseconds(0)));

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("504", response->headers().Status()->value().getStringView());
}

// Sends a request with a global timeout and per try timeout specified, sleeps
// for longer than the per try but slightly less than the global timeout.
// Ensures that two requests are attempted and a timeout is returned
// downstream.
TEST_P(HttpTimeoutIntegrationTest, PerTryTimeout) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"x-forwarded-for", "10.0.0.1"},
                              {"x-envoy-retry-on", "5xx"},
                              {"x-envoy-upstream-rq-timeout-ms", "500"},
                              {"x-envoy-upstream-rq-per-try-timeout-ms", "400"}});
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  codec_client_->sendData(*request_encoder_, 0, true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger per try timeout (but not global timeout).
  timeSystem().sleep(std::chrono::milliseconds(400));

  // Wait for a second request to be sent upstream
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger global timeout.
  timeSystem().sleep(std::chrono::milliseconds(100));
  response->waitForHeaders();

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("504", response->headers().Status()->value().getStringView());
}

} // namespace Envoy
