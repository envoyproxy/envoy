#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

using DrainCloseIntegrationTest = HttpProtocolIntegrationTest;

// Add a health check filter and verify correct behavior when draining.
TEST_P(DrainCloseIntegrationTest, DrainCloseGradual) {
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

TEST_P(DrainCloseIntegrationTest, DrainCloseImmediate) {
  drain_strategy_ = Server::DrainStrategy::Immediate;
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
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  response->waitForEndStream();

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }
}

TEST_P(DrainCloseIntegrationTest, AdminDrain) { testAdminDrain(downstreamProtocol()); }

TEST_P(DrainCloseIntegrationTest, AdminGracefulDrain) {
  drain_strategy_ = Server::DrainStrategy::Immediate;
  drain_time_ = std::chrono::seconds(999);
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  // The request is completed but the connection remains open.
  EXPECT_TRUE(codec_client_->connected());

  // Invoke /drain_listeners with graceful drain
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners?graceful", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  // With a 999s graceful drain period, the listener should still be open.
  EXPECT_EQ(test_server_->counter("listener_manager.listener_stopped")->value(), 0);

  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  // Connections will terminate on request complete
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecClient::Type::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }

  // New connections can still be made.
  auto second_codec_client_ = makeRawHttpConnection(makeClientConnection(http_port), absl::nullopt);
  EXPECT_TRUE(second_codec_client_->connected());

  // Invoke /drain_listeners and shut down listeners.
  second_codec_client_->rawConnection().close(Network::ConnectionCloseType::NoFlush);
  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  test_server_->waitForCounterEq("listener_manager.listener_stopped", 1);
  ASSERT_TRUE(waitForPortAvailable(http_port));
}

TEST_P(DrainCloseIntegrationTest, RepeatedAdminGracefulDrain) {
  // Use the default gradual probabilistic DrainStrategy so drainClose()
  // behaviour isn't conflated with whether the drain sequence has started.
  drain_time_ = std::chrono::seconds(999);
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);
  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();

  // Invoke /drain_listeners with graceful drain
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners?graceful", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");
  EXPECT_EQ(test_server_->counter("listener_manager.listener_stopped")->value(), 0);

  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners?graceful", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  response->waitForEndStream();
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  test_server_->waitForCounterEq("listener_manager.listener_stopped", 1);
  ASSERT_TRUE(waitForPortAvailable(http_port));
}

INSTANTIATE_TEST_SUITE_P(Protocols, DrainCloseIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2},
                             {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace Envoy
