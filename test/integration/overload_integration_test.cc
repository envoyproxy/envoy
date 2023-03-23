#include <unordered_map>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"

#include "test/integration/base_overload_integration_test.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/ssl_utility.h"

#include "absl/strings/str_cat.h"

using testing::InvokeWithoutArgs;

namespace Envoy {

using testing::HasSubstr;

class OverloadIntegrationTest : public BaseOverloadIntegrationTest,
                                public HttpProtocolIntegrationTest {
protected:
  void
  initializeOverloadManager(const envoy::config::overload::v3::OverloadAction& overload_action) {
    setupOverloadManagerConfig(overload_action);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
    initialize();
    updateResource(0);
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, OverloadIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2,
                              Http::CodecClient::Type::HTTP3},
                             {FakeHttpConnection::Type::HTTP1, FakeHttpConnection::Type::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(OverloadIntegrationTest, CloseStreamsWhenOverloaded) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.stop_accepting_requests"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.9
    )EOF"));

  // Put envoy in overloaded state and check that it drops new requests.
  // Test both header-only and header+body requests since the code paths are slightly different.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 1);

  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeRequestWithBody(request_headers, 10);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ("envoy overloaded", response->body());
  codec_client_->close();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ("envoy overloaded", response->body());
  codec_client_->close();

  // Deactivate overload state and check that new requests are accepted.
  updateResource(0.8);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0U, response->body().size());
}

TEST_P(OverloadIntegrationTest, DisableKeepaliveWhenOverloaded) {
  if (downstreamProtocol() != Http::CodecType::HTTP1) {
    return; // only relevant for downstream HTTP1.x connections
  }

  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.disable_http_keepalive"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.8
    )EOF"));

  // Put envoy in overloaded state and check that it disables keepalive
  updateResource(0.8);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.disable_http_keepalive.active", 1);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  auto response = sendRequestAndWaitForResponse(request_headers, 1, default_response_headers_, 1);
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("close", response->headers().getConnectionValue());

  // Deactivate overload state and check that keepalive is not disabled
  updateResource(0.7);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.disable_http_keepalive.active", 0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = sendRequestAndWaitForResponse(request_headers, 1, default_response_headers_, 1);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(nullptr, response->headers().Connection());
}

TEST_P(OverloadIntegrationTest, StopAcceptingConnectionsWhenOverloaded) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.stop_accepting_connections"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.95
    )EOF"));

  // Put envoy in overloaded state and check that it doesn't accept the new client connection.
  updateResource(0.95);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               1);
  IntegrationStreamDecoderPtr response;
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP3) {
    // For HTTP/3, excess connections are force-rejected.
    codec_client_ =
        makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt);
    EXPECT_TRUE(codec_client_->disconnected());
  } else {
    // For HTTP/2 and below, excess connection won't be accepted, but will hang out
    // in a pending state and resume below.
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
    EXPECT_FALSE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_,
                                                           std::chrono::milliseconds(1000)));
  }

  // Reduce load a little to allow the connection to be accepted.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_connections.active",
                               0);
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP3) {
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
    response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  }
  EXPECT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  EXPECT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 10));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "202"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("202", response->headers().getStatusValue());
  codec_client_->close();
}

class OverloadScaledTimerIntegrationTest : public OverloadIntegrationTest {
protected:
  void initializeOverloadManager(
      const envoy::config::overload::v3::ScaleTimersOverloadActionConfig& config) {
    envoy::config::overload::v3::OverloadAction overload_action =
        TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.reduce_timeouts"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          scaled:
            scaling_threshold: 0.5
            saturation_threshold: 0.9
    )EOF");
    overload_action.mutable_typed_config()->PackFrom(config);
    OverloadIntegrationTest::initializeOverloadManager(overload_action);
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, OverloadScaledTimerIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(OverloadScaledTimerIntegrationTest, CloseIdleHttpConnections) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: HTTP_DOWNSTREAM_CONNECTION_IDLE
          min_timeout: 5s
    )EOF"));

  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  // Create an HTTP connection and complete a request.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeRequestWithBody(request_headers, 10);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 10));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  // At this point, the connection should be idle but still open.
  ASSERT_TRUE(codec_client_->connected());

  // Set the load so the timer is reduced but not to the minimum value.
  updateResource(0.8);
  test_server_->waitForGaugeGe("overload.envoy.overload_actions.reduce_timeouts.scale_percent", 50);
  // Advancing past the minimum time shouldn't close the connection.
  timeSystem().advanceTimeWait(std::chrono::seconds(5));

  // Increase load so that the minimum time has now elapsed.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.reduce_timeouts.scale_percent",
                               100);

  // Wait for the proxy to notice and take action for the overload.
  test_server_->waitForCounterGe("http.config_test.downstream_cx_idle_timeout", 1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  if (GetParam().downstream_protocol == Http::CodecType::HTTP1) {
    // For HTTP1, Envoy will start draining but will wait to close the
    // connection. If a new stream comes in, it will set the connection header
    // to "close" on the response and close the connection after.
    auto response = codec_client_->makeRequestWithBody(request_headers, 10);
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 10));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ(response->headers().getConnectionValue(), "close");
  } else {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    EXPECT_TRUE(codec_client_->sawGoAway());
  }
  codec_client_->close();
}

TEST_P(OverloadScaledTimerIntegrationTest, CloseIdleHttpStream) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: HTTP_DOWNSTREAM_STREAM_IDLE
          min_timeout: 5s
    )EOF"));

  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};

  // Create an HTTP connection and start a request.
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeRequestWithBody(request_headers, 10);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  // At this point, Envoy is waiting for the upstream to respond, but that won't
  // happen before it hits the stream timeout.

  // Set the load so the timer is reduced but not to the minimum value.
  updateResource(0.8);
  test_server_->waitForGaugeGe("overload.envoy.overload_actions.reduce_timeouts.scale_percent", 50);
  // Advancing past the minimum time shouldn't end the stream.
  timeSystem().advanceTimeWait(std::chrono::seconds(5));

  // Increase load so that the minimum time has now elapsed.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.reduce_timeouts.scale_percent",
                               100);

  // Wait for the proxy to notice and take action for the overload.
  test_server_->waitForCounterGe("http.config_test.downstream_rq_idle_timeout", 1);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ(response->headers().getStatusValue(), "504");
  EXPECT_THAT(response->body(), HasSubstr("stream timeout"));
}

TEST_P(OverloadScaledTimerIntegrationTest, TlsHandshakeTimeout) {
  // Set up the Envoy to expect a TLS connection, with a 20 second timeout that can scale down to 5
  // seconds.
  config_helper_.addSslConfig();
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    auto* connect_timeout = filter_chain->mutable_transport_socket_connect_timeout();
    connect_timeout->set_seconds(20);
    connect_timeout->set_nanos(0);
  });
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: TRANSPORT_SOCKET_CONNECT
          min_timeout: 5s
    )EOF"));

  // Set up a delinquent transport socket that causes the dispatcher to exit on every read & write
  // instead of actually doing anything useful.
  auto bad_transport_socket = std::make_unique<NiceMock<Network::MockTransportSocket>>();
  Network::TransportSocketCallbacks* transport_callbacks;
  EXPECT_CALL(*bad_transport_socket, setTransportSocketCallbacks)
      .WillOnce(SaveArgAddress(&transport_callbacks));
  ON_CALL(*bad_transport_socket, doRead).WillByDefault(InvokeWithoutArgs([&] {
    Buffer::OwnedImpl buffer;
    transport_callbacks->connection().dispatcher().exit();
    // Read some amount of data; what's more important is whether the socket was remote-closed. That
    // needs to be propagated to the socket.
    return Network::IoResult{transport_callbacks->ioHandle().read(buffer, 2 * 1024).return_value_ ==
                                     0
                                 ? Network::PostIoAction::Close
                                 : Network::PostIoAction::KeepOpen,
                             0, false};
  }));
  ON_CALL(*bad_transport_socket, doWrite).WillByDefault(InvokeWithoutArgs([&] {
    transport_callbacks->connection().dispatcher().exit();
    return Network::IoResult{Network::PostIoAction::KeepOpen, 0, false};
  }));

  ConnectionStatusCallbacks connect_callbacks;
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("http"));
  auto bad_ssl_client =
      dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                          std::move(bad_transport_socket), nullptr, nullptr);
  bad_ssl_client->addConnectionCallbacks(connect_callbacks);
  bad_ssl_client->enableHalfClose(true);
  bad_ssl_client->connect();

  // Run the dispatcher until it exits due to a read/write.
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);

  // At this point, the connection should be idle but the SSL handshake isn't done.
  EXPECT_FALSE(connect_callbacks.connected());

  // Set the load so the timer is reduced but not to the minimum value.
  updateResource(0.8);
  test_server_->waitForGaugeGe("overload.envoy.overload_actions.reduce_timeouts.scale_percent", 50);

  // Advancing past the minimum time shouldn't close the connection, but it shouldn't complete it
  // either.
  timeSystem().advanceTimeWait(std::chrono::seconds(5));
  EXPECT_FALSE(connect_callbacks.connected());

  // At this point, Envoy has been waiting for the (bad) client to finish the TLS handshake for 5
  // seconds. Increase the load so that the minimum time has now elapsed. This should cause Envoy to
  // close the connection on its end.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.reduce_timeouts.scale_percent",
                               100);

  // The bad client will continue attempting to read, eventually noticing the remote close and
  // closing the connection.
  while (!connect_callbacks.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // The transport-level connection was never completed, and the connection was closed.
  EXPECT_FALSE(connect_callbacks.connected());
  EXPECT_TRUE(connect_callbacks.closed());
}

} // namespace Envoy
