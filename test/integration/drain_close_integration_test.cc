#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace {

using testing::Eq;

class DrainCloseIntegrationTest : public HttpProtocolIntegrationTest {
protected:
  // Decides drain-close by polling the listener's DrainDecision rather than from the
  // connection-level drain notification. Tests that start a drain sequence by calling the drain
  // manager directly need this: the connection-level path is driven by the notification that
  // InstanceBase::drainListeners() and the /drain_listeners?graceful handler push to listeners
  // alongside startDrainSequence(), and a direct call to the drain manager does not emit it.
  // Must be called before initialize().
  void useLegacyDrainClose() {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.use_connection_event_drain",
                                      "false");
  }
};

TEST_P(DrainCloseIntegrationTest, DrainCloseGradual) {
  useLegacyDrainClose();
  autonomous_upstream_ = true;
  // The probability of drain close increases over time. With a high timeout,
  // the probability will be very low, but the rapid retries prevent this from
  // increasing total test time.
  drain_time_ = std::chrono::seconds(100);
  initialize();

  absl::Notification drain_sequence_started;
  test_server_->server().dispatcher().post([this, &drain_sequence_started]() {
    test_server_->drainManager().startDrainSequence(Network::DrainDirection::All, [] {});
    drain_sequence_started.Notify();
  });
  drain_sequence_started.WaitForNotification();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_FALSE(codec_client_->disconnected());

  IntegrationStreamDecoderPtr response;
  while (!test_server_->counter("http.config_test.downstream_cx_drain_close")->value()) {
    response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
  }
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_drain_close")->value(), 1L);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }
}

TEST_P(DrainCloseIntegrationTest, DrainCloseImmediate) {
  useLegacyDrainClose();
  autonomous_upstream_ = true;
  drain_strategy_ = Server::DrainStrategy::Immediate;
  drain_time_ = std::chrono::seconds(100);
  initialize();

  absl::Notification drain_sequence_started;
  test_server_->server().dispatcher().post([this, &drain_sequence_started]() {
    test_server_->drainManager().startDrainSequence(Network::DrainDirection::All, [] {});
    drain_sequence_started.Notify();
  });
  drain_sequence_started.WaitForNotification();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_FALSE(codec_client_->disconnected());

  IntegrationStreamDecoderPtr response;
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }
}

// Equivalent of DrainCloseGradual on the connection-level drain path: the drain is pushed to the
// listener, which replays it to this connection when it is accepted, and the gradual ramp is then
// computed from the drain start time carried by that notification.
TEST_P(DrainCloseIntegrationTest, DrainCloseGradualViaConnectionDrain) {
  autonomous_upstream_ = true;
  // As in DrainCloseGradual: a high drain time keeps the per-response probability low, and the
  // rapid retries keep total test time down. Note the elapsed drain time is truncated to whole
  // seconds, so the probability is zero until one second after the drain started and the loop
  // below simply spins until then.
  drain_time_ = std::chrono::seconds(100);
  initialize();

  startServerDrain();

  // The connection is accepted after the drain started, so it is notified from
  // ActiveStreamListenerBase::newConnection() rather than by the fan-out over live connections.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_FALSE(codec_client_->disconnected());

  IntegrationStreamDecoderPtr response;
  while (!test_server_->counter("http.config_test.downstream_cx_drain_close")->value()) {
    response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
  }
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_drain_close")->value(), 1L);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }
}

// Equivalent of DrainCloseImmediate on the connection-level drain path: an Immediate strategy is
// carried on the drain notification, so the first response after the connection is notified
// drain-closes regardless of the drain time.
TEST_P(DrainCloseIntegrationTest, DrainCloseImmediateViaConnectionDrain) {
  autonomous_upstream_ = true;
  drain_strategy_ = Server::DrainStrategy::Immediate;
  drain_time_ = std::chrono::seconds(100);
  initialize();

  startServerDrain();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  EXPECT_FALSE(codec_client_->disconnected());

  IntegrationStreamDecoderPtr response =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(test_server_->counter("http.config_test.downstream_cx_drain_close")->value(), 1L);
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
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
  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  // Connections will terminate on request complete
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }

  // New connections can still be made.
  auto second_codec_client_ = makeRawHttpConnection(makeClientConnection(http_port), std::nullopt);
  EXPECT_TRUE(second_codec_client_->connected());

  // Invoke /drain_listeners and shut down listeners.
  second_codec_client_->rawConnection().close(Network::ConnectionCloseType::NoFlush);
  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  test_server_->waitForCounter("listener_manager.listener_stopped", Eq(1));
  ASSERT_TRUE(waitForPortAvailable(http_port));
}

TEST_P(DrainCloseIntegrationTest, RepeatedAdminGracefulDrain) {
  // Use the default gradual probabilistic DrainStrategy so drainClose()
  // behaviour isn't conflated with whether the drain sequence has started.
  drain_time_ = std::chrono::seconds(999);
  initialize();
  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());

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
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  test_server_->waitForCounter("listener_manager.listener_stopped", Eq(1));
  ASSERT_TRUE(waitForPortAvailable(http_port));
}

TEST_P(DrainCloseIntegrationTest, AdminGracefulDrainSkipExit) {
  drain_strategy_ = Server::DrainStrategy::Immediate;
  drain_time_ = std::chrono::seconds(1);
  initialize();
  uint32_t http_port = lookupPort("http");
  codec_client_ = makeHttpConnection(http_port);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));
  // The request is completed but the connection remains open.
  EXPECT_TRUE(codec_client_->connected());

  // Invoke /drain_listeners with graceful drain
  BufferingStreamDecoderPtr admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners?graceful&skip_exit", "", downstreamProtocol(),
      version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  // Listeners should remain open
  EXPECT_EQ(test_server_->counter("listener_manager.listener_stopped")->value(), 0);

  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest(0);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_THAT(response->headers(), Http::HttpStatusIs("200"));

  // Connections will terminate on request complete
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP2) {
    EXPECT_TRUE(codec_client_->sawGoAway());
  } else {
    EXPECT_EQ("close", response->headers().getConnectionValue());
  }

  // New connections can still be made.
  auto second_codec_client_ = makeRawHttpConnection(makeClientConnection(http_port), std::nullopt);
  EXPECT_TRUE(second_codec_client_->connected());

  // Invoke /drain_listeners and shut down listeners.
  second_codec_client_->rawConnection().close(Network::ConnectionCloseType::NoFlush);
  admin_response = IntegrationUtil::makeSingleRequest(
      lookupPort("admin"), "POST", "/drain_listeners", "", downstreamProtocol(), version_);
  EXPECT_EQ(admin_response->headers().Status()->value().getStringView(), "200");

  test_server_->waitForCounter("listener_manager.listener_stopped", Eq(1));
  ASSERT_TRUE(waitForPortAvailable(http_port));
}

INSTANTIATE_TEST_SUITE_P(Protocols, DrainCloseIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace Envoy
