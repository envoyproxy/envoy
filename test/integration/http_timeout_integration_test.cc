#include "test/integration/http_timeout_integration_test.h"

#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

namespace Envoy {

using testing::HasSubstr;

INSTANTIATE_TEST_SUITE_P(IpVersions, HttpTimeoutIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Sends a request with a global timeout specified, sleeps for longer than the
// timeout, and ensures that a timeout is received.
TEST_P(HttpTimeoutIntegrationTest, GlobalTimeout) {
  config_helper_.addConfigModifier(configureProxyStatus());
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
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
  timeSystem().advanceTimeWait(std::chrono::milliseconds(501));

  // Ensure we got a timeout downstream and canceled the upstream request.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(upstream_request_->waitForReset(std::chrono::seconds(15)));

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("504", response->headers().getStatusValue());
  EXPECT_EQ(response->headers().getProxyStatusValue(),
            "envoy; error=http_response_timeout; details=\"response_timeout; UT\"");
}

// Testing that `x-envoy-expected-timeout-ms` header, set by egress envoy, is respected by ingress
// envoy when `respect_expected_rq_timeout` field is enabled. Sends a request with a global timeout
// specified, sleeps for longer than the timeout, and ensures that a timeout is received.
TEST_P(HttpTimeoutIntegrationTest, UseTimeoutSetByEgressEnvoy) {
  enableRespectExpectedRqTimeout(true);
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-upstream-rq-timeout-ms", "500"},
                                     {"x-envoy-expected-rq-timeout-ms", "300"}});
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  codec_client_->sendData(*request_encoder_, 0, true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger global timeout, populated from `x-envoy-expected-rq-timeout-ms` header.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(301));

  // Ensure we got a timeout downstream and canceled the upstream request.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(upstream_request_->waitForReset(std::chrono::seconds(15)));

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("504", response->headers().getStatusValue());
}

// Testing that ingress envoy derives new timeout value and sets `x-envoy-expected-timeout-ms`
// header, when timeout has not been set by egress envoy and `respect_expected_rq_timeout` field is
// enabled. Sends a request with a global timeout specified, sleeps for longer than the timeout, and
// ensures that a timeout is received.
TEST_P(HttpTimeoutIntegrationTest, DeriveTimeoutInIngressEnvoy) {
  enableRespectExpectedRqTimeout(true);
  initialize();
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
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

  // Trigger global timeout, populated from `x-envoy-expected-rq-timeout-ms` header.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(501));

  // Ensure we got a timeout downstream and canceled the upstream request.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(upstream_request_->waitForReset(std::chrono::seconds(15)));

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("504", response->headers().getStatusValue());
}

// Testing that `x-envoy-expected-timeout-ms` header, set by egress envoy, is ignored by ingress
// envoy and new value is derived. Sends a request with a global timeout specified,
// sleeps for longer than the timeout, and ensures that a timeout is received.
TEST_P(HttpTimeoutIntegrationTest, IgnoreTimeoutSetByEgressEnvoy) {
  enableRespectExpectedRqTimeout(false);
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-upstream-rq-timeout-ms", "500"},
                                     {"x-envoy-expected-rq-timeout-ms", "600"}});
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  codec_client_->sendData(*request_encoder_, 0, true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger global timeout, populated from `x-envoy-expected-rq-timeout-ms` header.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(501));

  // Ensure we got a timeout downstream and canceled the upstream request.
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(upstream_request_->waitForReset(std::chrono::seconds(15)));

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("504", response->headers().getStatusValue());
}

// Regression test for https://github.com/envoyproxy/envoy/issues/7154 in which
// resetStream() was only called after a response timeout for upstream requests
// that had not received headers yet. This meant that decodeData might be
// called on a destroyed UpstreamRequest.
TEST_P(HttpTimeoutIntegrationTest, GlobalTimeoutAfterHeadersBeforeBodyResetsUpstream) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "10.0.0.1"},
                                                 {"x-envoy-upstream-rq-timeout-ms", "100"}};
  auto encoder_decoder = codec_client_->startRequest(request_headers);
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  codec_client_->sendData(*request_encoder_, 100, true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Respond with headers, not end of stream.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);

  response->waitForHeaders();
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Trigger global timeout.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(200));

  ASSERT_TRUE(upstream_request_->waitForReset(std::chrono::seconds(15)));

  ASSERT_TRUE(response->waitForReset());

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
}

// Sends a request with a global timeout and per try timeout specified, sleeps
// for longer than the per try but slightly less than the global timeout.
// Ensures that two requests are attempted and a timeout is returned
// downstream.
TEST_P(HttpTimeoutIntegrationTest, PerTryTimeout) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
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

  // Trigger per try timeout (but not global timeout) and wait for reset.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(400));
  ASSERT_TRUE(upstream_request_->waitForReset());

  // Wait for a second request to be sent upstream. Max retry backoff is 25ms so advance time that
  // much.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(25));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger global timeout.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(100));
  ASSERT_TRUE(response->waitForEndStream());

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("504", response->headers().getStatusValue());
}

// Sends a request with a per try timeout specified but no global timeout.
// Ensures that two requests are attempted and a timeout is returned
// downstream.
TEST_P(HttpTimeoutIntegrationTest, PerTryTimeoutWithoutGlobalTimeout) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"},
                                     {"x-envoy-upstream-rq-timeout-ms", "0"},
                                     {"x-envoy-upstream-rq-per-try-timeout-ms", "50"}});
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  codec_client_->sendData(*request_encoder_, 0, true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger per try timeout (but not global timeout) and wait for reset.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(50));
  ASSERT_TRUE(upstream_request_->waitForReset());

  // Wait for a second request to be sent upstream. Max retry backoff is 25ms so advance time that
  // much. This is always less than the next request's per try timeout.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(25));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Encode 200 response headers for the first (timed out) request.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

void HttpTimeoutIntegrationTest::testIsTimeoutRetryHeader(bool use_hedged_retry) {
  auto host = config_helper_.createVirtualHost("example.com", "/test_retry");
  host.set_include_is_timeout_retry_header(true);
  config_helper_.addVirtualHost(host);

  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"},
      {":path", "/test_retry"},
      {":scheme", "http"},
      {":authority", "example.com"},
      {"x-forwarded-for", "10.0.0.1"},
      {"x-envoy-retry-on", "5xx"},
      {"x-envoy-hedge-on-per-try-timeout", use_hedged_retry ? "true" : "fase"},
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
  timeSystem().advanceTimeWait(std::chrono::milliseconds(400));

  // Trigger retry (there's a 25ms backoff before it's issued).
  timeSystem().advanceTimeWait(std::chrono::milliseconds(26));

  // Wait for a second request to be sent upstream
  FakeStreamPtr upstream_request2;

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request2));

  ASSERT_TRUE(upstream_request2->waitForHeadersComplete());

  // Expect the x-envoy-is-timeout-header to set to indicate to the upstream this is a retry
  // initiated by a previous per try timeout.
  EXPECT_EQ(upstream_request2->headers().getEnvoyIsTimeoutRetryValue(), "true");

  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  if (use_hedged_retry) {
    upstream_request_->encodeHeaders(response_headers, true);
    ASSERT_TRUE(response->waitForEndStream());
    // The second request should be reset since we used the response from the first request.
    ASSERT_TRUE(upstream_request2->waitForReset(std::chrono::seconds(15)));
  } else {
    upstream_request2->encodeHeaders(response_headers, true);
    ASSERT_TRUE(response->waitForEndStream());
  }

  codec_client_->close();
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// With hedge_on_per_try_timeout enabled via config, sends a request with a
// global timeout and per try timeout specified, sleeps for longer than the per
// try but slightly less than the global timeout. We expect a retry to be sent
// upstream and the x-envoy-is-timeout-retry request header to be set to true.
TEST_P(HttpTimeoutIntegrationTest, IsTimeoutRetryHeaderHedgedRetry) {
  testIsTimeoutRetryHeader(true);
}

// Sends a request with a per try timeout specified, sleeps for longer than the per
// try but slightly less than the global timeout. We expect a retry to be sent
// upstream and the x-envoy-is-timeout-retry request header to be set to true.
TEST_P(HttpTimeoutIntegrationTest, IsTimeoutRetryHeaderPerTryTimeout) {
  testIsTimeoutRetryHeader(false);
}

// With hedge_on_per_try_timeout enabled via config, sends a request with a
// global timeout and per try timeout specified, sleeps for longer than the per
// try but slightly less than the global timeout. We then have the first
// upstream request return headers and expect those to be returned downstream
// (which proves the request was not canceled when the timeout was hit).
TEST_P(HttpTimeoutIntegrationTest, HedgedPerTryTimeout) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/test/long/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"},
                                     {"x-envoy-hedge-on-per-try-timeout", "true"},
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
  timeSystem().advanceTimeWait(std::chrono::milliseconds(400));

  // Trigger retry (there's a 25ms backoff before it's issued).
  timeSystem().advanceTimeWait(std::chrono::milliseconds(26));

  // Wait for a second request to be sent upstream
  FakeStreamPtr upstream_request2;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request2));
  ASSERT_TRUE(upstream_request2->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  // Encode 200 response headers for the first (timed out) request.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, true);

  ASSERT_TRUE(response->waitForEndStream());

  // The second request should be reset since we used the response from the first request.
  ASSERT_TRUE(upstream_request2->waitForReset(std::chrono::seconds(15)));

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(HttpTimeoutIntegrationTest, HedgedPerTryTimeoutWithBodyNoBufferFirstRequestWins) {
  testRouterRequestAndResponseWithHedgedPerTryTimeout(1024, 512, true);
}

TEST_P(HttpTimeoutIntegrationTest, HedgedPerTryTimeoutWithBodyNoBufferSecondRequestWins) {
  testRouterRequestAndResponseWithHedgedPerTryTimeout(1024, 512, false);
}

TEST_P(HttpTimeoutIntegrationTest,
       HedgedPerTryTimeoutLowUpstreamBufferLimitLargeRequestFirstRequestWins) {
  config_helper_.setBufferLimits(1024, 1024 * 1024); // Set buffer limits upstream and downstream.
  testRouterRequestAndResponseWithHedgedPerTryTimeout(1024 * 1024, 1024, true);
}

TEST_P(HttpTimeoutIntegrationTest,
       HedgedPerTryTimeoutLowUpstreamBufferLimitLargeRequestSecondRequestWins) {
  config_helper_.setBufferLimits(1024, 1024 * 1024); // Set buffer limits upstream and downstream.
  testRouterRequestAndResponseWithHedgedPerTryTimeout(1024 * 1024, 1024, false);
}

TEST_P(HttpTimeoutIntegrationTest,
       HedgedPerTryTimeoutLowDownstreamBufferLimitLargeResponseFirstRequestWins) {
  config_helper_.setBufferLimits(1024 * 1024, 1024); // Set buffer limits upstream and downstream.
  testRouterRequestAndResponseWithHedgedPerTryTimeout(1024, 1024 * 1024, true);
}

TEST_P(HttpTimeoutIntegrationTest,
       HedgedPerTryTimeoutLowDownstreamBufferLimitLargeResponseSecondRequestWins) {
  config_helper_.setBufferLimits(1024 * 1024, 1024); // Set buffer limits upstream and downstream.
  testRouterRequestAndResponseWithHedgedPerTryTimeout(1024, 1024 * 1024, false);
}

// Sends a request with x-envoy-hedge-on-per-try-timeout, sleeps (with
// simulated time) for longer than the per try timeout but shorter than the
// global timeout, asserts that a retry is sent, and then responds with a 200
// response on the original request and ensures the downstream sees it.
// Request/response/header size are configurable to test flow control. If
// first_request_wins is true, then the "winning" response will be sent in
// response to the first (timed out) request. If false, the second request will
// get the good response.
void HttpTimeoutIntegrationTest::testRouterRequestAndResponseWithHedgedPerTryTimeout(
    uint64_t request_size, uint64_t response_size, bool first_request_wins) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-forwarded-for", "10.0.0.1"},
                                                 {"x-envoy-retry-on", "5xx"},
                                                 {"x-envoy-hedge-on-per-try-timeout", "true"},
                                                 {"x-envoy-upstream-rq-timeout-ms", "5000"},
                                                 {"x-envoy-upstream-rq-per-try-timeout-ms", "400"}};
  auto encoder_decoder = codec_client_->startRequest(request_headers);

  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  codec_client_->sendData(*request_encoder_, request_size, true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger per try timeout (but not global timeout).
  timeSystem().advanceTimeWait(std::chrono::milliseconds(400));

  FakeStreamPtr upstream_request2;
  // Trigger retry (there's a 25ms backoff before it's issued).
  timeSystem().advanceTimeWait(std::chrono::milliseconds(26));

  // Wait for a second request to be sent upstream
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request2));
  ASSERT_TRUE(upstream_request2->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  if (first_request_wins) {
    // Encode 200 response headers for the first (timed out) request.
    upstream_request_->encodeHeaders(response_headers, response_size == 0);
  } else {
    // Encode 200 response headers for the second request.
    upstream_request2->encodeHeaders(response_headers, response_size == 0);
  }

  response->waitForHeaders();

  if (first_request_wins) {
    // The second request should be reset since we used the response from the first request.
    ASSERT_TRUE(upstream_request2->waitForReset(std::chrono::seconds(15)));
  } else {
    // The first request should be reset since we used the response from the second request.
    ASSERT_TRUE(upstream_request_->waitForReset(std::chrono::seconds(15)));
  }

  if (response_size) {
    if (first_request_wins) {
      upstream_request_->encodeData(response_size, true);
    } else {
      upstream_request2->encodeData(response_size, true);
    }
  }

  ASSERT_TRUE(response->waitForEndStream());

  codec_client_->close();

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_TRUE(upstream_request2->complete());
  if (first_request_wins) {
    EXPECT_EQ(request_size, upstream_request_->bodyLength());
  } else {
    EXPECT_EQ(request_size, upstream_request2->bodyLength());
  }

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Starts a request with a header timeout specified, sleeps for longer than the
// timeout, and ensures that a timeout is received.
TEST_P(HttpTimeoutIntegrationTest, RequestHeaderTimeout) {
  if (downstreamProtocol() != Http::CodecType::HTTP1) {
    // This test requires that the downstream be using HTTP1.
    return;
  }

  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        auto* request_headers_timeout = hcm.mutable_request_headers_timeout();
        request_headers_timeout->set_seconds(1);
        request_headers_timeout->set_nanos(0);
      });
  initialize();

  const std::string input_request = ("GET / HTTP/1.1\r\n"
                                     // Omit trailing \r\n that would indicate the end of headers.
                                     "Host: localhost\r\n");
  std::string response;

  auto connection_driver = createConnectionDriver(
      lookupPort("http"), input_request,
      [&response](Network::ClientConnection&, const Buffer::Instance& data) -> void {
        response.append(data.toString());
      });

  while (!connection_driver->allBytesSent()) {
    ASSERT_TRUE(connection_driver->run(Event::Dispatcher::RunType::NonBlock));
  }
  test_server_->waitForGaugeGe("http.config_test.downstream_rq_active", 1);
  ASSERT_FALSE(connection_driver->closed());

  timeSystem().advanceTimeWait(std::chrono::milliseconds(1001));
  ASSERT_TRUE(connection_driver->run());

  // The upstream should send a 40x response and send a local reply.
  EXPECT_TRUE(connection_driver->closed());
  EXPECT_THAT(response, AllOf(HasSubstr("408"), HasSubstr("header")));
}

// Validate that Envoy correctly handles per try and per try IDLE timeouts
// that are firing within the backoff interval.
TEST_P(HttpTimeoutIntegrationTest, OriginalRequestCompletesBeforeBackoffTimer) {
  auto host = config_helper_.createVirtualHost("example.com", "/test_retry");
  host.set_include_is_timeout_retry_header(true);
  config_helper_.addVirtualHost(host);
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->mutable_virtual_hosts(1);
        auto* route = virtual_host->mutable_routes(0)->mutable_route();
        auto* retry_policy = route->mutable_retry_policy();
        retry_policy->mutable_per_try_idle_timeout()->set_seconds(0);
        // per try IDLE timeout is 400 ms
        retry_policy->mutable_per_try_idle_timeout()->set_nanos(400 * 1000 * 1000);
      });
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"},
      {":path", "/test_retry"},
      {":scheme", "http"},
      {":authority", "example.com"},
      {"x-forwarded-for", "10.0.0.1"},
      {"x-envoy-retry-on", "5xx"},
      // Enable hedge_on_per_try_timeout so that original request is not reset
      {"x-envoy-hedge-on-per-try-timeout", "true"},
      {"x-envoy-upstream-rq-timeout-ms", "500"},
      // Make per try timeout the same as the per try idle timeout
      // NOTE: it can be a bit longer, within the back off interval
      {"x-envoy-upstream-rq-per-try-timeout-ms", "400"}});
  auto response = std::move(encoder_decoder.second);
  request_encoder_ = &encoder_decoder.first;

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  codec_client_->sendData(*request_encoder_, 0, true);

  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Trigger per try timeout (but not global timeout). This will actually trigger
  // both IDLE and request timeouts in the same I/O operation.
  timeSystem().advanceTimeWait(std::chrono::milliseconds(400));

  // Trigger retry (there's a 25ms backoff before it's issued).
  timeSystem().advanceTimeWait(std::chrono::milliseconds(26));

  // Wait for a second request to be sent upstream
  FakeStreamPtr upstream_request2;

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request2));

  ASSERT_TRUE(upstream_request2->waitForHeadersComplete());

  // Expect the x-envoy-is-timeout-header to set to indicate to the upstream this is a retry
  // initiated by a previous per try timeout.
  EXPECT_EQ(upstream_request2->headers().getEnvoyIsTimeoutRetryValue(), "true");

  ASSERT_TRUE(upstream_request2->waitForEndStream(*dispatcher_));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  // Respond to the second request (it does not matter which request gets response).
  upstream_request2->encodeHeaders(response_headers, true);
  ASSERT_TRUE(response->waitForEndStream());

  // The first request should be reset since we used the response from the second request.
  ASSERT_TRUE(upstream_request_->waitForReset(std::chrono::seconds(15)));

  codec_client_->close();
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace Envoy
