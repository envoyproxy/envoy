#include <unordered_map>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/overload/v3/overload.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/base_overload_integration_test.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/test_runtime.h"

#include "absl/strings/str_cat.h"

using testing::InvokeWithoutArgs;

namespace Envoy {

using testing::HasSubstr;

class OverloadIntegrationTest : public BaseOverloadIntegrationTest,
                                public HttpProtocolIntegrationTest {
protected:
  void initializeOverloadManager(const envoy::config::overload::v3::OverloadAction& overload_action,
                                 absl::optional<bool> appendLocalOverloadHeader = absl::nullopt) {
    setupOverloadManagerConfig(overload_action);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });

    if (appendLocalOverloadHeader.has_value() && appendLocalOverloadHeader.value()) {
      config_helper_.addConfigModifier(
          [=](envoy::extensions::filters::network::http_connection_manager::v3::
                  HttpConnectionManager& cm) -> void { cm.set_append_local_overload(true); });
    }

    initialize();
    updateResource(0);
  }

  void initializeWithBypassOverloadManager(
      const envoy::config::overload::v3::OverloadAction& overload_action) {
    setupOverloadManagerConfig(overload_action);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_bypass_overload_manager(true);
      auto* new_listener = bootstrap.mutable_static_resources()->add_listeners();
      new_listener->CopyFrom(*listener);
      new_listener->set_name("http_2");
      new_listener->set_bypass_overload_manager(false);
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

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"}};
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeRequestWithBody(request_headers, 10);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  // Verify that no local overload header is added by default
  EXPECT_EQ(true, response->headers().get(Http::Headers::get().EnvoyLocalOverloaded).empty());
  EXPECT_EQ("envoy overloaded", response->body());
  codec_client_->close();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(true, response->headers().get(Http::Headers::get().EnvoyLocalOverloaded).empty());
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

TEST_P(OverloadIntegrationTest, AppendLocalOverloadHeader) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.stop_accepting_requests"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.9
    )EOF"),
      true);

  // Put envoy in overloaded state and check that it drops new requests and the local overload is
  // correctly added. Test both header-only and header+body requests since the code paths are
  // slightly different.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 1);

  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"}};
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeRequestWithBody(request_headers, 10);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(Http::Headers::get().EnvoyOverloadedValues.True,
            response->headers()
                .get(Http::Headers::get().EnvoyLocalOverloaded)[0]
                ->value()
                .getStringView());
  EXPECT_EQ("envoy overloaded", response->body());
  codec_client_->close();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(Http::Headers::get().EnvoyOverloadedValues.True,
            response->headers()
                .get(Http::Headers::get().EnvoyLocalOverloaded)[0]
                ->value()
                .getStringView());
  EXPECT_EQ("envoy overloaded", response->body());
  codec_client_->close();
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
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "sni.lyft.com"}};
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

TEST_P(OverloadIntegrationTest, BypassOverloadManagerTest) {
  initializeWithBypassOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::OverloadAction>(R"EOF(
      name: "envoy.overload_actions.stop_accepting_requests"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.9
    )EOF"));

  // Put envoy in overloaded state and validate that it doesn't drop new requests
  // because we chose to bypass the overload manager on this listener.
  updateResource(1);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.stop_accepting_requests.active", 1);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(0U, response->body().size());
  codec_client_->close();

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http_2"))));
  response = codec_client_->makeRequestWithBody(default_request_headers_, 10);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());
  // Verify that no local overload header is added by default
  EXPECT_EQ(true, response->headers().get(Http::Headers::get().EnvoyLocalOverloaded).empty());
  EXPECT_EQ("envoy overloaded", response->body());
  codec_client_->close();
}

class Http2RawFrameOverloadIntegrationTest : public BaseOverloadIntegrationTest,
                                             public Http2RawFrameIntegrationTest,
                                             public testing::Test {
public:
  Http2RawFrameOverloadIntegrationTest()
      : Http2RawFrameIntegrationTest(Envoy::Network::Address::IpVersion::v4) {
    setupHttp2ImplOverrides(Envoy::Http2Impl::Oghttp2);
  }

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
    setupOverloadManagerConfig(overload_action);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
  }
};

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

  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "sni.lyft.com"}};

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

TEST_P(OverloadScaledTimerIntegrationTest, MaxConnectionDuration) {
  config_helper_.addConfigModifier(
      [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              cm) -> void {
        cm.mutable_common_http_protocol_options()->mutable_max_connection_duration()->MergeFrom(
            ProtobufUtil::TimeUtil::SecondsToDuration(20));
      });

  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: HTTP_DOWNSTREAM_CONNECTION_MAX
          min_timeout: 5s
    )EOF"));

  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "sni.lyft.com"}};

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
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
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

TEST_P(OverloadScaledTimerIntegrationTest, Http1SafeMaxConnectionDuration) {
  config_helper_.addConfigModifier(
      [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              cm) -> void {
        cm.set_http1_safe_max_connection_duration(true);
        // Drain timeout is not used for max_connection_duration if
        // http1_safe_max_connection_duration is enabled.
        cm.mutable_drain_timeout()->MergeFrom(ProtobufUtil::TimeUtil::SecondsToDuration(0));
        cm.mutable_common_http_protocol_options()->mutable_max_connection_duration()->MergeFrom(
            ProtobufUtil::TimeUtil::SecondsToDuration(20));
      });

  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: HTTP_DOWNSTREAM_CONNECTION_MAX
          min_timeout: 5s
    )EOF"));

  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "sni.lyft.com"}};

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
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 1);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  if (GetParam().downstream_protocol == Http::CodecType::HTTP1) {
    // There's no drain timer. We can even advance time again and the connection will still be
    // intact.
    timeSystem().advanceTimeWait(std::chrono::seconds(5));
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

TEST_P(OverloadScaledTimerIntegrationTest, HTTP3CloseIdleHttpConnectionsDuringHandshake) {
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP3) {
    return;
  }
  TestScopedRuntime scoped_runtime;

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* proof_source_config = bootstrap.mutable_static_resources()
                                    ->mutable_listeners(0)
                                    ->mutable_udp_listener_config()
                                    ->mutable_quic_options()
                                    ->mutable_proof_source_config();
    proof_source_config->set_name("envoy.quic.proof_source.pending_signing");
    proof_source_config->mutable_typed_config();
  });
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: HTTP_DOWNSTREAM_CONNECTION_IDLE
          min_timeout: 3s
    )EOF"));

  // Set the load so the timer is reduced but not to the minimum value.
  updateResource(0.8);
  test_server_->waitForGaugeGe("overload.envoy.overload_actions.reduce_timeouts.scale_percent", 50);
  // Create an HTTP connection without finishing the handshake.
  codec_client_ = makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                                        absl::nullopt,
                                        /*wait_till_connected=*/false);
  EXPECT_FALSE(codec_client_->connected());

  // Advancing past the minimum time shouldn't close the connection.
  timeSystem().advanceTimeWait(std::chrono::seconds(3));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(codec_client_->connected());
  EXPECT_FALSE(codec_client_->disconnected());

  // Increase load more so that the timer is reduced to the minimum.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.reduce_timeouts.scale_percent",
                               100);

  // Create another HTTP connection without finishing handshake.
  IntegrationCodecClientPtr codec_client2 =
      makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                            absl::nullopt, /*wait_till_connected=*/false);
  EXPECT_FALSE(codec_client2->connected());
  // Advancing past the minimum time and wait for the proxy to notice and close both connections.
  timeSystem().advanceTimeWait(std::chrono::seconds(3));
  test_server_->waitForCounterGe("http.config_test.downstream_cx_idle_timeout", 2);
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_FALSE(codec_client_->sawGoAway());
  EXPECT_FALSE(codec_client2->connected());
  ASSERT_TRUE(codec_client2->waitForDisconnect());
  EXPECT_FALSE(codec_client2->sawGoAway());
}

TEST_P(OverloadScaledTimerIntegrationTest, HTTP3CloseMaxDurationHttpConnectionsDuringHandshake) {
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP3) {
    return;
  }
  TestScopedRuntime scoped_runtime;

  config_helper_.addConfigModifier(
      [=](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              cm) -> void {
        cm.mutable_common_http_protocol_options()->mutable_max_connection_duration()->MergeFrom(
            ProtobufUtil::TimeUtil::SecondsToDuration(20));
      });

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* proof_source_config = bootstrap.mutable_static_resources()
                                    ->mutable_listeners(0)
                                    ->mutable_udp_listener_config()
                                    ->mutable_quic_options()
                                    ->mutable_proof_source_config();
    proof_source_config->set_name("envoy.quic.proof_source.pending_signing");
    proof_source_config->mutable_typed_config();
  });
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: HTTP_DOWNSTREAM_CONNECTION_MAX
          min_timeout: 3s
    )EOF"));

  // Set the load so the timer is reduced but not to the minimum value.
  updateResource(0.8);
  test_server_->waitForGaugeGe("overload.envoy.overload_actions.reduce_timeouts.scale_percent", 50);
  // Create an HTTP connection without finishing the handshake.
  codec_client_ = makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                                        absl::nullopt,
                                        /*wait_till_connected=*/false);
  EXPECT_FALSE(codec_client_->connected());

  // Advancing past the minimum time shouldn't close the connection.
  timeSystem().advanceTimeWait(std::chrono::seconds(3));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_FALSE(codec_client_->connected());
  EXPECT_FALSE(codec_client_->disconnected());

  // Increase load more so that the timer is reduced to the minimum.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.reduce_timeouts.scale_percent",
                               100);

  // Create another HTTP connection without finishing handshake.
  IntegrationCodecClientPtr codec_client2 =
      makeRawHttpConnection(makeClientConnection((lookupPort("http"))), absl::nullopt,
                            absl::nullopt, /*wait_till_connected=*/false);
  EXPECT_FALSE(codec_client2->connected());
  // Advancing past the minimum time and wait for the proxy to notice and close both connections.
  timeSystem().advanceTimeWait(std::chrono::seconds(3));
  test_server_->waitForCounterGe("http.config_test.downstream_cx_max_duration_reached", 2);
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_FALSE(codec_client_->sawGoAway());
  EXPECT_FALSE(codec_client2->connected());
  ASSERT_TRUE(codec_client2->waitForDisconnect());
  EXPECT_FALSE(codec_client2->sawGoAway());
}

TEST_P(OverloadScaledTimerIntegrationTest, CloseIdleHttpStream) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: HTTP_DOWNSTREAM_STREAM_IDLE
          min_timeout: 5s
    )EOF"));

  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "sni.lyft.com"}};

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

TEST_F(Http2RawFrameOverloadIntegrationTest, FlushTimeoutWhenDownstreamBlocked) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::ScaleTimersOverloadActionConfig>(R"EOF(
      timer_scale_factors:
        - timer: HTTP_DOWNSTREAM_STREAM_FLUSH
          min_timeout: 1s
    )EOF"));

  // Create a downstream connection with an initial stream window size of 1 rather than the default
  // 65535.
  beginSession(Http2Frame::makeSettingsFrame(
      Http2Frame::SettingsFlags::None,
      {{static_cast<uint16_t>(Http2Frame::Setting::InitialWindowSize), 1}}));

  // Simulate increased load so the timer is reduced to the minimum value.
  updateResource(0.9);
  test_server_->waitForGaugeEq("overload.envoy.overload_actions.reduce_timeouts.scale_percent",
                               100);

  // Send a headers-only request.
  sendFrame(Http2Frame::makeRequest(1, /*host=*/"sni.lyft.com", /*path=*/"/test/long/url"));

  // Respond from upstream with more data than the downstream window will allow.
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(2, true);

  // Read the response headers.
  Http2Frame response_headers = readFrame();
  EXPECT_EQ(response_headers.streamId(), 1);
  EXPECT_EQ(response_headers.type(), Http2Frame::Type::Headers);

  // Downstream receive window is 1, so the Envoy will encode 1 byte and buffer 1 byte.
  Http2Frame response_data = readFrame();
  EXPECT_EQ(response_data.streamId(), 1);
  EXPECT_EQ(response_data.type(), Http2Frame::Type::Data);
  EXPECT_EQ(response_data.payloadSize(), 1);

  // The client DOES NOT send a window update, so eventually Envoy's flush timer will fire...
  timeSystem().advanceTimeWait(std::chrono::seconds(2));
  test_server_->waitForCounterGe("http2.tx_flush_timeout", 1);

  // ... Which will cause the stream to be reset.
  Http2Frame reset_frame = readFrame();
  EXPECT_EQ(reset_frame.streamId(), 1);
  EXPECT_EQ(reset_frame.type(), Http2Frame::Type::RstStream);

  tcp_client_->close();
  test_server_->waitForGaugeEq("http.config_test.downstream_rq_active", 0);
}

TEST_P(OverloadScaledTimerIntegrationTest, TlsHandshakeTimeout) {
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP3 ||
      upstreamProtocol() == Http::CodecClient::Type::HTTP3) {
    // TODO(#26236): Fix this test for H3.
    return;
  }
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

class LoadShedPointIntegrationTest : public BaseOverloadIntegrationTest,
                                     public HttpProtocolIntegrationTest {
protected:
  void initializeOverloadManager(const envoy::config::overload::v3::LoadShedPoint& config) {
    setupOverloadManagerConfig(config);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
    initialize();
    updateResource(0);
  }
  void
  initializeWithBypassOverloadManager(const envoy::config::overload::v3::LoadShedPoint& config) {
    setupOverloadManagerConfig(config);
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      *bootstrap.mutable_overload_manager() = this->overload_manager_config_;
    });
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      listener->set_bypass_overload_manager(true);
      // add a listener that doesn't bypass overload manager.
      auto* new_listener = bootstrap.mutable_static_resources()->add_listeners();
      new_listener->CopyFrom(*listener);
      new_listener->set_name("http_2");
      new_listener->set_bypass_overload_manager(false);
    });
    initialize();
    updateResource(0);
  }
};

INSTANTIATE_TEST_SUITE_P(Protocols, LoadShedPointIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecClient::Type::HTTP1, Http::CodecClient::Type::HTTP2,
                              Http::CodecClient::Type::HTTP3},
                             {FakeHttpConnection::Type::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(LoadShedPointIntegrationTest, ListenerAcceptShedsLoad) {
  // QUIC uses UDP, not TCP.
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP3) {
    return;
  }
  autonomous_upstream_ = true;
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.tcp_listener_accept"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  // Put envoy in overloaded state and check that it rejects the new client connection.
  updateResource(0.95);
  test_server_->waitForGaugeEq("overload.envoy.load_shed_points.tcp_listener_accept.scale_percent",
                               100);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  if (version_ == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_cx_overload_reject", 1);
  } else {
    test_server_->waitForCounterEq("listener.[__1]_0.downstream_cx_overload_reject", 1);
  }
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Disable overload, we should allow connections.
  updateResource(0.80);
  test_server_->waitForGaugeEq("overload.envoy.load_shed_points.tcp_listener_accept.scale_percent",
                               0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
}

TEST_P(LoadShedPointIntegrationTest, AcceptNewHttpStreamShedsLoad) {
  autonomous_upstream_ = true;
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http_connection_manager_decode_headers"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  // Put envoy in overloaded state and check that it sends a local reply for the
  // new stream.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http_connection_manager_decode_headers.scale_percent", 100);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response_that_will_be_local_reply =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 1);
  ASSERT_TRUE(response_that_will_be_local_reply->waitForEndStream());
  EXPECT_EQ(response_that_will_be_local_reply->headers().getStatusValue(), "503");

  // Disable overload, Envoy should proxy the request.
  updateResource(0.80);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http_connection_manager_decode_headers.scale_percent", 0);

  auto response_that_will_be_proxied =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response_that_will_be_proxied->waitForEndStream());
  EXPECT_EQ(response_that_will_be_proxied->headers().getStatusValue(), "200");
  // Should not be incremented as we didn't reject the request.
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 1);
}

TEST_P(LoadShedPointIntegrationTest, Http1ServerDispatchAbortShedsLoadWhenNewRequest) {
  // Test only applies to HTTP1.
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP1) {
    return;
  }
  autonomous_upstream_ = true;
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http1_server_abort_dispatch"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 0);

  // Put envoy in overloaded state and check that the dispatch fails.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http1_server_abort_dispatch.scale_percent", 100);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto [encoder, decoder] = codec_client_->startRequest(default_request_headers_);

  // We should get rejected local reply and connection close.
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 1);
  ASSERT_TRUE(decoder->waitForEndStream());
  EXPECT_EQ(decoder->headers().getStatusValue(), "500");
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Disable overload, we should allow connections.
  updateResource(0.80);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http1_server_abort_dispatch.scale_percent", 0);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");
}

// Test using 100-continue to start the response before doing triggering Overload.
// Even though Envoy has encoded the 1xx headers, 1xx headers are not the actual response
// so Envoy should still send the local reply when shedding load.
TEST_P(LoadShedPointIntegrationTest,
       Http1ServerDispatchAbortShedsLoadWithLocalReplyWhen1xxResponseStarted) {
  // Test only applies to HTTP1.
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP1) {
    return;
  }

  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http1_server_abort_dispatch"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 0);

  // Start the 100-continue request
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":path", "/dynamo/url"},
                                                                 {":scheme", "http"},
                                                                 {":authority", "sni.lyft.com"},
                                                                 {"expect", "100-contINUE"}});

  // Wait for 100-continue
  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  // The continue headers should arrive immediately.
  response->waitFor1xxHeaders();
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));

  // Put envoy in overloaded state and check that it rejects the continuing
  // dispatch.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http1_server_abort_dispatch.scale_percent", 100);
  codec_client_->sendData(*request_encoder_, 10, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "500");
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 1);
}

TEST_P(LoadShedPointIntegrationTest, Http1ServerDispatchShouldNotAbortEncodingUpstreamResponse) {
  // Test only applies to HTTP1.
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP1) {
    return;
  }
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http1_server_abort_dispatch"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  waitForNextUpstreamRequest();

  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Put envoy in overloaded state, the response should succeed.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http1_server_abort_dispatch.scale_percent", 100);
  upstream_request_->encodeData(100, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 0);
}

TEST_P(LoadShedPointIntegrationTest, Http1ServerDispatchAbortClosesConnectionWhenResponseStarted) {
  // Test only applies to HTTP1.
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP1) {
    return;
  }
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http1_server_abort_dispatch"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"},
      {":path", "/dynamo/url"},
      {":scheme", "http"},
      {":authority", "sni.lyft.com"},
  });

  request_encoder_ = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  upstream_request_->encodeHeaders(default_response_headers_, false);
  response->waitForHeaders();

  // Put envoy in overloaded state, the next dispatch should fail.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http1_server_abort_dispatch.scale_percent", 100);

  Buffer::OwnedImpl data("hello world");
  request_encoder_->encodeData(data, true);

  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_FALSE(response->complete());
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 1);
}

TEST_P(LoadShedPointIntegrationTest, Http2ServerDispatchSendsGoAwayCompletingPendingRequests) {
  // Test only applies to HTTP2.
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP2) {
    return;
  }
  autonomous_upstream_ = true;
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http2_server_go_away_on_dispatch"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto [first_request_encoder, first_request_decoder] =
      codec_client_->startRequest(default_request_headers_);
  test_server_->waitForCounterEq("http.config_test.downstream_rq_http2_total", 1);

  // Put envoy in overloaded state to send GOAWAY frames.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http2_server_go_away_on_dispatch.scale_percent", 100);

  auto second_request_decoder = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Wait for reply of the first request which should be allowed to complete.
  // The downstream should also receive the GOAWAY.
  Buffer::OwnedImpl first_request_body{"foo"};
  first_request_encoder.encodeData(first_request_body, true);
  ASSERT_TRUE(first_request_decoder->waitForEndStream());

  EXPECT_TRUE(codec_client_->sawGoAway());
  test_server_->waitForCounterEq("http2.goaway_sent", 1);

  // The GOAWAY gets submitted with the first created stream as the last stream
  // that will be processed on this connection, so the second stream's frames
  // are ignored.
  EXPECT_FALSE(second_request_decoder->complete());

  updateResource(0.80);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http2_server_go_away_on_dispatch.scale_percent", 0);
}

TEST_P(LoadShedPointIntegrationTest, Http2ServerDispatchSendsGoAwayAndClosesConnection) {
  // Test only applies to HTTP2.
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP2) {
    return;
  }
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http2_server_go_away_and_close_on_dispatch"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto [first_request_encoder, first_request_decoder] =
      codec_client_->startRequest(default_request_headers_);
  test_server_->waitForCounterEq("http.config_test.downstream_rq_http2_total", 1);

  // Put envoy in overloaded state to send GOAWAY frames and close the connection.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http2_server_go_away_and_close_on_dispatch.scale_percent",
      100);

  auto second_request_decoder = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The downstream should receive the GOAWAY and the connection should be closed.
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(codec_client_->sawGoAway());
  test_server_->waitForCounterEq("http2.goaway_sent", 1);
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 1);

  // The second request will not complete.
  EXPECT_FALSE(second_request_decoder->complete());
}

TEST_P(LoadShedPointIntegrationTest, HttpConnectionMnagerCloseConnectionCreatingCodec) {
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP3) {
    return;
  }
  autonomous_upstream_ = true;
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.hcm_ondata_creating_codec"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.hcm_ondata_creating_codec.scale_percent", 100);
  auto encoder_decoder = codec_client_->startRequest(default_request_headers_);

  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 1);
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  updateResource(0.80);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.hcm_ondata_creating_codec.scale_percent", 0);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  test_server_->waitForCounterEq("http.config_test.downstream_rq_overload_close", 1);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "200");
}

TEST_P(LoadShedPointIntegrationTest, HttpDownstreamFilterLoadShed) {
  autonomous_upstream_ = true;
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http_downstream_filter_check"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

  // Put envoy in overloaded state and check that it sends a local reply from router.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http_downstream_filter_check.scale_percent", 100);
  auto response_with_local_reply = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response_with_local_reply->waitForEndStream());
  EXPECT_EQ(response_with_local_reply->headers().getStatusValue(), "503");

  updateResource(0.80);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http_downstream_filter_check.scale_percent", 0);

  auto response_that_is_proxied = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response_that_is_proxied->waitForEndStream());
  EXPECT_EQ(response_that_is_proxied->headers().getStatusValue(), "200");
}

TEST_P(LoadShedPointIntegrationTest, ConnectionPoolNewConnectionLoadShed) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.connection_pool_new_connection"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.connection_pool_new_connection.scale_percent", 100);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(response->headers().getStatusValue(), "503");
  codec_client_->close();
}

TEST_P(LoadShedPointIntegrationTest, ConnectionPoolLoadShedWithExistingConnections) {
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.connection_pool_new_connection"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  updateResource(0.80);
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ(response1->headers().getStatusValue(), "200");

  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.connection_pool_new_connection.scale_percent", 100);

  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ(response2->headers().getStatusValue(), "200");
  codec_client_->close();
}

TEST_P(LoadShedPointIntegrationTest, HttpConnManagerDoesNotShedLoadWhenBypassed) {
  // QUIC uses UDP, not TCP.
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP3) {
    return;
  }
  autonomous_upstream_ = true;
  initializeWithBypassOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http_connection_manager_decode_headers"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  // Put envoy in overloaded state and check that
  // the listener that bypasses the overload manager does not reject the new request.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http_connection_manager_decode_headers.scale_percent", 100);
  auto codec_client = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response_that_is_proxied = codec_client->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response_that_is_proxied->waitForEndStream());
  EXPECT_EQ(response_that_is_proxied->headers().getStatusValue(), "200");
  codec_client->close();

  // Check that the other listener gets its requests rejected.
  auto codec_client2 = makeHttpConnection(makeClientConnection((lookupPort("http_2"))));
  auto response_gets_local_reply = codec_client2->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response_gets_local_reply->waitForEndStream());
  EXPECT_EQ(response_gets_local_reply->headers().getStatusValue(), "503");
  codec_client2->close();
}

TEST_P(LoadShedPointIntegrationTest, ListenerAcceptDoesNotShedLoadWhenBypassed) {
  // QUIC uses UDP, not TCP.
  if (downstreamProtocol() == Http::CodecClient::Type::HTTP3) {
    return;
  }
  autonomous_upstream_ = true;
  initializeWithBypassOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.tcp_listener_accept"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  // Put envoy in overloaded state and check that it does not reject the new client connection
  // on the listener that bypasses overload manager.
  updateResource(0.95);
  test_server_->waitForGaugeEq("overload.envoy.load_shed_points.tcp_listener_accept.scale_percent",
                               100);

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  codec_client_->close();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // on the other listener though, we should reject the connection
  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http_2"))));
  if (version_ == Network::Address::IpVersion::v4) {
    test_server_->waitForCounterEq("listener.127.0.0.1_0.downstream_cx_overload_reject", 1);
  } else {
    test_server_->waitForCounterEq("listener.[__1]_0.downstream_cx_overload_reject", 1);
  }
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

TEST_P(LoadShedPointIntegrationTest, Http3ServerDispatchSendsGoAwayAndClosesConnection) {
  // Test only applies to HTTP3.
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP3) {
    return;
  }
  autonomous_upstream_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http3_server_go_away_and_close_on_dispatch"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto [first_request_encoder, first_request_decoder] =
      codec_client_->startRequest(default_request_headers_);
  test_server_->waitForCounterEq("http.config_test.downstream_rq_http3_total", 1);

  // Put envoy in overloaded state to send GOAWAY frames and close the
  // connection.
  updateResource(0.95);
  test_server_->waitForGaugeEq("overload.envoy.load_shed_points.http3_server_go_away_and_close_on_"
                               "dispatch.scale_percent",
                               100);

  auto second_request_decoder = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The downstream should receive the GOAWAY and the connection should be
  // closed.
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  EXPECT_TRUE(codec_client_->sawGoAway());

  // The second request will not complete.
  EXPECT_FALSE(second_request_decoder->complete());
}

TEST_P(LoadShedPointIntegrationTest, Http3ServerDispatchSendsGoAwayCompletingPendingRequests) {
  // Test only applies to HTTP3.
  if (downstreamProtocol() != Http::CodecClient::Type::HTTP3) {
    return;
  }
  autonomous_upstream_ = true;
  initializeOverloadManager(
      TestUtility::parseYaml<envoy::config::overload::v3::LoadShedPoint>(R"EOF(
      name: "envoy.load_shed_points.http3_server_go_away_on_dispatch"
      triggers:
        - name: "envoy.resource_monitors.testonly.fake_resource_monitor"
          threshold:
            value: 0.90
    )EOF"));

  codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  auto [first_request_encoder, first_request_decoder] =
      codec_client_->startRequest(default_request_headers_);
  test_server_->waitForCounterEq("http.config_test.downstream_rq_http3_total", 1);

  // Put envoy in overloaded state to send GOAWAY frames.
  updateResource(0.95);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http3_server_go_away_on_dispatch.scale_"
      "percent",
      100);

  auto second_request_decoder = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Wait for reply of the first request which should be allowed to complete.
  // The downstream should also receive the GOAWAY.
  Buffer::OwnedImpl first_request_body{"foo"};
  first_request_encoder.encodeData(first_request_body, true);
  ASSERT_TRUE(first_request_decoder->waitForEndStream());

  EXPECT_TRUE(codec_client_->sawGoAway());

  // SendH3GoAway will process pending streams up to maximum possible,
  // so the second request should also complete.
  EXPECT_TRUE(second_request_decoder->waitForEndStream());
  EXPECT_TRUE(second_request_decoder->complete());

  codec_client_->close();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  updateResource(0.80);
  test_server_->waitForGaugeEq(
      "overload.envoy.load_shed_points.http3_server_go_away_on_dispatch.scale_"
      "percent",
      0);
}

} // namespace Envoy
