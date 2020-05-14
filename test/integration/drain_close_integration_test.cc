#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

using DrainCloseIntegrationTest = HttpProtocolIntegrationTest;

// Add a health check filter and verify correct behavior when draining.
TEST_P(DrainCloseIntegrationTest, DrainClose) {
  // The probability of drain close increases over time. With a high timeout,
  // the probability will be very low, but the rapid retries prevent this from
  // increasing total test time.
  drain_time_ = std::chrono::seconds(100);
  config_helper_.addFilter(ConfigHelper::defaultHealthCheckFilter());
  initialize();

  absl::Notification drain_sequence_started;
  test_server_->server().dispatcher().post([this, &drain_sequence_started]() {
    test_server_->drainManager().startDrainSequence([] {});
    drain_sequence_started.Notify();
  });
  drain_sequence_started.WaitForNotification();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_FALSE(codec_client_->disconnected());

  IntegrationStreamDecoderPtr response;
  while (!test_server_->counter("http.config_test.downstream_cx_drain_close")->value()) {
    response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    response->waitForEndStream();
  }
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_drain_close")->value(), 1L);

  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }
}

TEST_P(DrainCloseIntegrationTest, AdminDrain) {
  initialize();

  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);
  Http::TestRequestHeaderMapImpl request_headers{{":method", "HEAD"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"}};
  IntegrationStreamDecoderPtr response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest(0);
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Invoke drain listeners endpoint and validate that we can still work on inflight requests.
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", downstreamProtocol(), version_);
  EXPECT_TRUE(admin_response->complete());
  EXPECT_EQ("200", admin_response->headers().getStatusValue());
  EXPECT_EQ("OK\n", admin_response->body());

  upstream_request_->encodeData(512, true);

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Wait for the response to be read by the codec client.
  response->waitForEndStream();

  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  // Validate that the listeners have been stopped.
  test_server_->waitForCounterEq("listener_manager.listener_stopped", 1);
}

INSTANTIATE_TEST_SUITE_P(Protocols, DrainCloseIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2},
                             {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

}  // namespace
}  // namespace Envoy
