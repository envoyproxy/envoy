#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Terminating CONNECT and sending raw TCP upstream.
class ConnectTerminationIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ConnectTerminationIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {
    enableHalfClose(true);
  }

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          ConfigHelper::setConnectConfig(hcm, true, allow_post_);

          if (enable_timeout_) {
            hcm.mutable_stream_idle_timeout()->set_seconds(0);
            hcm.mutable_stream_idle_timeout()->set_nanos(200 * 1000 * 1000);
          }
          if (exact_match_) {
            auto* route_config = hcm.mutable_route_config();
            ASSERT_EQ(1, route_config->virtual_hosts_size());
            route_config->mutable_virtual_hosts(0)->clear_domains();
            route_config->mutable_virtual_hosts(0)->add_domains("host:80");
          }
        });
    HttpIntegrationTest::initialize();
  }

  void setUpConnection() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(connect_headers_);
    request_encoder_ = &encoder_decoder.first;
    response_ = std::move(encoder_decoder.second);
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_raw_upstream_connection_));
    response_->waitForHeaders();
  }

  void sendBidirectionalData(const char* downstream_send_data = "hello",
                             const char* upstream_received_data = "hello",
                             const char* upstream_send_data = "there!",
                             const char* downstream_received_data = "there!") {
    // Send some data upstream.
    codec_client_->sendData(*request_encoder_, downstream_send_data, false);
    ASSERT_TRUE(fake_raw_upstream_connection_->waitForData(
        FakeRawConnection::waitForInexactMatch(upstream_received_data)));

    // Send some data downstream.
    ASSERT_TRUE(fake_raw_upstream_connection_->write(upstream_send_data));
    response_->waitForBodyData(strlen(downstream_received_data));
    EXPECT_EQ(downstream_received_data, response_->body());
  }

  Http::TestRequestHeaderMapImpl connect_headers_{{":method", "CONNECT"},
                                                  {":path", "/"},
                                                  {":protocol", "bytestream"},
                                                  {":scheme", "https"},
                                                  {":authority", "host:80"}};
  FakeRawConnectionPtr fake_raw_upstream_connection_;
  IntegrationStreamDecoderPtr response_;
  bool enable_timeout_{};
  bool exact_match_{};
  bool allow_post_{};
};

TEST_P(ConnectTerminationIntegrationTest, Basic) {
  initialize();

  setUpConnection();
  sendBidirectionalData("hello", "hello", "there!", "there!");
  // Send a second set of data to make sure for example headers are only sent once.
  sendBidirectionalData(",bye", "hello,bye", "ack", "there!ack");

  // Send an end stream. This should result in half close upstream.
  codec_client_->sendData(*request_encoder_, "", true);
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());

  // Now send a FIN from upstream. This should result in clean shutdown downstream.
  ASSERT_TRUE(fake_raw_upstream_connection_->close());
  response_->waitForEndStream();
  ASSERT_FALSE(response_->reset());
}

TEST_P(ConnectTerminationIntegrationTest, BasicAllowPost) {
  allow_post_ = true;
  initialize();

  // Use POST request.
  connect_headers_.setMethod("POST");
  connect_headers_.removeProtocol();

  setUpConnection();
  sendBidirectionalData("hello", "hello", "there!", "there!");
  // Send a second set of data to make sure for example headers are only sent once.
  sendBidirectionalData(",bye", "hello,bye", "ack", "there!ack");

  // Send an end stream. This should result in half close upstream.
  codec_client_->sendData(*request_encoder_, "", true);
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());

  // Now send a FIN from upstream. This should result in clean shutdown downstream.
  ASSERT_TRUE(fake_raw_upstream_connection_->close());
  response_->waitForEndStream();
  ASSERT_FALSE(response_->reset());
}

TEST_P(ConnectTerminationIntegrationTest, UsingHostMatch) {
  exact_match_ = true;
  initialize();

  connect_headers_.removePath();

  setUpConnection();
  sendBidirectionalData("hello", "hello", "there!", "there!");
  // Send a second set of data to make sure for example headers are only sent once.
  sendBidirectionalData(",bye", "hello,bye", "ack", "there!ack");

  // Send an end stream. This should result in half close upstream.
  codec_client_->sendData(*request_encoder_, "", true);
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());

  // Now send a FIN from upstream. This should result in clean shutdown downstream.
  ASSERT_TRUE(fake_raw_upstream_connection_->close());
  response_->waitForEndStream();
  ASSERT_FALSE(response_->reset());
}

TEST_P(ConnectTerminationIntegrationTest, DownstreamClose) {
  initialize();

  setUpConnection();
  sendBidirectionalData();

  // Tear down by closing the client connection.
  codec_client_->close();
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());
}

TEST_P(ConnectTerminationIntegrationTest, DownstreamReset) {
  initialize();

  setUpConnection();
  sendBidirectionalData();

  // Tear down by resetting the client stream.
  codec_client_->sendReset(*request_encoder_);
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());
}

TEST_P(ConnectTerminationIntegrationTest, UpstreamClose) {
  initialize();

  setUpConnection();
  sendBidirectionalData();

  // Tear down by closing the upstream connection.
  ASSERT_TRUE(fake_raw_upstream_connection_->close());
  response_->waitForReset();
}

TEST_P(ConnectTerminationIntegrationTest, TestTimeout) {
  enable_timeout_ = true;
  initialize();

  setUpConnection();

  // Wait for the timeout to close the connection.
  response_->waitForReset();
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());
}

TEST_P(ConnectTerminationIntegrationTest, BuggyHeaders) {
  initialize();

  // Sending a header-only request is probably buggy, but rather than having a
  // special corner case it is treated as a regular half close.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  response_ = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "CONNECT"},
                                     {":path", "/"},
                                     {":protocol", "bytestream"},
                                     {":scheme", "https"},
                                     {":authority", "host:80"}});
  // If the connection is established (created, set to half close, and then the
  // FIN arrives), make sure the FIN arrives, and send a FIN from upstream.
  if (fake_upstreams_[0]->waitForRawConnection(fake_raw_upstream_connection_) &&
      fake_raw_upstream_connection_->connected()) {
    ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());
    ASSERT_TRUE(fake_raw_upstream_connection_->close());
  }

  // Either with early close, or half close, the FIN from upstream should result
  // in clean stream teardown.
  response_->waitForEndStream();
  ASSERT_FALSE(response_->reset());
}

TEST_P(ConnectTerminationIntegrationTest, BasicMaxStreamDuration) {
  setUpstreamProtocol(upstreamProtocol());
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_common_http_protocol_options()
        ->mutable_max_stream_duration()
        ->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  initialize();
  setUpConnection();
  sendBidirectionalData();

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_max_duration_reached", 1);

  if (downstream_protocol_ == Http::CodecClient::Type::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    response_->waitForReset();
    codec_client_->close();
  }
}

// For this class, forward the CONNECT request upstream
class ProxyingConnectIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void { ConfigHelper::setConnectConfig(hcm, false, false); });

    HttpProtocolIntegrationTest::initialize();
  }

  Http::TestRequestHeaderMapImpl connect_headers_{{":method", "CONNECT"},
                                                  {":path", "/"},
                                                  {":protocol", "bytestream"},
                                                  {":scheme", "https"},
                                                  {":authority", "host:80"}};
  IntegrationStreamDecoderPtr response_;
};

INSTANTIATE_TEST_SUITE_P(Protocols, ProxyingConnectIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(ProxyingConnectIntegrationTest, ProxyConnect) {
  initialize();

  // Send request headers.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(connect_headers_);
  request_encoder_ = &encoder_decoder.first;
  response_ = std::move(encoder_decoder.second);

  // Wait for them to arrive upstream.
  AssertionResult result =
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Method)[0]->value(), "CONNECT");
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    EXPECT_TRUE(upstream_request_->headers().get(Http::Headers::get().Protocol).empty());
  } else {
    EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Protocol)[0]->value(),
              "bytestream");
  }

  // Send response headers
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Wait for them to arrive downstream.
  response_->waitForHeaders();
  EXPECT_EQ("200", response_->headers().getStatusValue());

  // Make sure that even once the response has started, that data can continue to go upstream.
  codec_client_->sendData(*request_encoder_, "hello", false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Also test upstream to downstream data.
  upstream_request_->encodeData(12, false);
  response_->waitForBodyData(12);

  cleanupUpstreamAndDownstream();
}

TEST_P(ProxyingConnectIntegrationTest, ProxyConnectWithIP) {
  initialize();

  // Send request headers.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  connect_headers_.setHost("1.2.3.4:80");
  auto encoder_decoder = codec_client_->startRequest(connect_headers_);
  request_encoder_ = &encoder_decoder.first;
  response_ = std::move(encoder_decoder.second);

  // Wait for them to arrive upstream.
  AssertionResult result =
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Method)[0]->value(), "CONNECT");
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    EXPECT_TRUE(upstream_request_->headers().get(Http::Headers::get().Protocol).empty());
  } else {
    EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Protocol)[0]->value(),
              "bytestream");
  }

  // Send response headers
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Wait for them to arrive downstream.
  response_->waitForHeaders();
  EXPECT_EQ("200", response_->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectTerminationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

using Params = std::tuple<Network::Address::IpVersion, FakeHttpConnection::Type, bool>;

// Tunneling downstream TCP over an upstream HTTP CONNECT tunnel.
class TcpTunnelingIntegrationTest : public testing::TestWithParam<Params>,
                                    public HttpIntegrationTest {
public:
  TcpTunnelingIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, std::get<0>(GetParam())) {}

  static std::string paramsToString(const testing::TestParamInfo<Params>& p) {
    return fmt::format(
        "{}_{}_{}", std::get<0>(p.param) == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6",
        std::get<1>(p.param) == FakeHttpConnection::Type::HTTP1 ? "HTTP1Upstream" : "HTTP2Upstream",
        std::get<2>(p.param) ? "WaitConnectResponse" : "DoNotWaitConnectResponse");
  }

  void SetUp() override {
    wait_for_connect_response_ = std::get<2>(GetParam());
    enableHalfClose(true);
    setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
    setUpstreamProtocol(std::get<1>(GetParam()));

    if (wait_for_connect_response_) {
      config_helper_.addRuntimeOverride(
          "envoy.reloadable_features.http_upstream_wait_connect_response", "true");
    } else {
      config_helper_.addRuntimeOverride(
          "envoy.reloadable_features.http_upstream_wait_connect_response", "false");
    }
    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
          proxy_config.set_stat_prefix("tcp_stats");
          proxy_config.set_cluster("cluster_0");
          proxy_config.mutable_tunneling_config()->set_hostname("host.com:80");

          auto* listener = bootstrap.mutable_static_resources()->add_listeners();
          listener->set_name("tcp_proxy");
          auto* socket_address = listener->mutable_address()->mutable_socket_address();
          socket_address->set_address(
              Network::Test::getLoopbackAddressString(std::get<0>(GetParam())));
          socket_address->set_port_value(0);

          auto* filter_chain = listener->add_filter_chains();
          auto* filter = filter_chain->add_filters();
          filter->mutable_typed_config()->PackFrom(proxy_config);
          filter->set_name("envoy.filters.network.tcp_proxy");
        });
  }
  bool wait_for_connect_response_{};
};

TEST_P(TcpTunnelingIntegrationTest, Basic) {
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send upgrade headers downstream, fully establishing the connection.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send some data from downstream to upstream, and make sure it goes through.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Send data from upstream to downstream.
  upstream_request_->encodeData(12, false);
  ASSERT_TRUE(tcp_client->waitForData(12));

  // Now send more data and close the TCP client. This should be treated as half close, so the data
  // should go through.
  ASSERT_TRUE(tcp_client->write("hello", false));
  tcp_client->close();
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    // If the upstream now sends 'end stream' the connection is fully closed.
    upstream_request_->encodeData(0, true);
  }
}

TEST_P(TcpTunnelingIntegrationTest, BasicUsePost) {
  // Enable using POST.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("host.com:80");
    proxy_config.mutable_tunneling_config()->set_use_post(true);

    auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();
    for (auto& listener : *listeners) {
      if (listener.name() != "tcp_proxy") {
        continue;
      }
      auto* filter_chain = listener.mutable_filter_chains(0);
      auto* filter = filter_chain->mutable_filters(0);
      filter->mutable_typed_config()->PackFrom(proxy_config);
      break;
    }
  });

  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Method)[0]->value(), "POST");

  // Send upgrade headers downstream, fully establishing the connection.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send some data from downstream to upstream, and make sure it goes through.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Send data from upstream to downstream.
  upstream_request_->encodeData(12, false);
  ASSERT_TRUE(tcp_client->waitForData(12));

  // Now send more data and close the TCP client. This should be treated as half close, so the data
  // should go through.
  ASSERT_TRUE(tcp_client->write("hello", false));
  tcp_client->close();
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    // If the upstream now sends 'end stream' the connection is fully closed.
    upstream_request_->encodeData(0, true);
  }
}

TEST_P(TcpTunnelingIntegrationTest, InvalidResponseHeaders) {
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send invalid response_ headers, and verify that the client disconnects and
  // upstream gets a stream reset.
  default_response_headers_.setStatus(enumToInt(Http::Code::ServiceUnavailable));
  upstream_request_->encodeHeaders(default_response_headers_, false);
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  // The connection should be fully closed, but the client has no way of knowing
  // that. Ensure the FIN is read and clean up state.
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

TEST_P(TcpTunnelingIntegrationTest, CloseUpstreamFirst) {
  initialize();

  // Establish a connection.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send data in both directions.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Send data from upstream to downstream with an end stream and make sure the data is received
  // before the connection is half-closed.
  upstream_request_->encodeData(12, true);
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->close());
  }
  ASSERT_TRUE(tcp_client->waitForData(12));
  tcp_client->waitForHalfClose();

  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    tcp_client->close();
  } else {
    // Attempt to send data upstream.
    // should go through.
    ASSERT_TRUE(tcp_client->write("hello", false));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

    ASSERT_TRUE(tcp_client->write("hello", true));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  }
}

TEST_P(TcpTunnelingIntegrationTest, ResetStreamTest) {
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    return;
  }
  enableHalfClose(false);
  initialize();

  // Establish a connection.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Reset the stream.
  upstream_request_->encodeResetStream();
  tcp_client->waitForDisconnect();
}

TEST_P(TcpTunnelingIntegrationTest, TestIdletimeoutWithLargeOutstandingData) {
  enableHalfClose(false);
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(1);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(
        config_blob
            ->Is<API_NO_BOOST(envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy)>());
    auto tcp_proxy_config = MessageUtil::anyConvert<API_NO_BOOST(
        envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy)>(*config_blob);
    tcp_proxy_config.mutable_idle_timeout()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(500))
            .count());
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  std::string data(1024 * 16, 'a');
  ASSERT_TRUE(tcp_client->write(data));
  upstream_request_->encodeData(data, false);

  tcp_client->waitForDisconnect();
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    tcp_client->close();
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
}

// Test that a downstream flush works correctly (all data is flushed)
TEST_P(TcpTunnelingIntegrationTest, TcpProxyDownstreamFlush) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size / 4, size / 4);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  tcp_client->readDisable(true);
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(tcp_client->write("hello", false));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

    upstream_request_->encodeData(data, true);
    ASSERT_TRUE(fake_upstream_connection_->close());
  } else {
    ASSERT_TRUE(tcp_client->write("", true));

    // This ensures that readDisable(true) has been run on its thread
    // before tcp_client starts writing.
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    upstream_request_->encodeData(data, true);
  }

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);
  tcp_client->readDisable(false);
  tcp_client->waitForData(data);
  tcp_client->waitForHalfClose();
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    tcp_client->close();
  }
}

// Test that an upstream flush works correctly (all data is flushed)
TEST_P(TcpTunnelingIntegrationTest, TcpProxyUpstreamFlush) {
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size, size);
  initialize();

  std::string data(size, 'a');
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->readDisable(true);
  upstream_request_->encodeData("hello", false);

  // This ensures that fake_upstream_connection->readDisable has been run on its thread
  // before tcp_client starts writing.
  ASSERT_TRUE(tcp_client->waitForData(5));

  ASSERT_TRUE(tcp_client->write(data, true));
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    tcp_client->close();

    upstream_request_->readDisable(false);
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, size));
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    // Note that upstream_flush_active will *not* be incremented for the HTTP
    // tunneling case. The data is already written to the stream, so no drainer
    // is necessary.
    upstream_request_->readDisable(false);
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, size));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeData("world", true);
    tcp_client->waitForHalfClose();
  }
}

// Test that h2 connection is reused.
TEST_P(TcpTunnelingIntegrationTest, H2ConnectionReuse) {
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    return;
  }
  initialize();

  // Establish a connection.
  IntegrationTcpClientPtr tcp_client1 = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send data in both directions.
  ASSERT_TRUE(tcp_client1->write("hello1", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello1"));

  // Send data from upstream to downstream with an end stream and make sure the data is received
  // before the connection is half-closed.
  upstream_request_->encodeData("world1", true);
  tcp_client1->waitForData("world1");
  tcp_client1->waitForHalfClose();
  tcp_client1->close();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Establish a new connection.
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("tcp_proxy"));

  // The new CONNECT stream is established in the existing h2 connection.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  ASSERT_TRUE(tcp_client2->write("hello2", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello2"));

  // Send data from upstream to downstream with an end stream and make sure the data is received
  // before the connection is half-closed.
  upstream_request_->encodeData("world2", true);
  tcp_client2->waitForData("world2");
  tcp_client2->waitForHalfClose();
  tcp_client2->close();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
}

// Test that with HTTP1 we have no connection reuse with downstream close.
TEST_P(TcpTunnelingIntegrationTest, H1NoConnectionReuse) {
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP2) {
    return;
  }
  initialize();

  // Establish a connection.
  IntegrationTcpClientPtr tcp_client1 = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send data in both directions.
  ASSERT_TRUE(tcp_client1->write("hello1", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello1"));

  // Send data from upstream to downstream and close the connection
  // from downstream.
  upstream_request_->encodeData("world1", false);
  tcp_client1->waitForData("world1");
  tcp_client1->close();

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Establish a new connection.
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("tcp_proxy"));
  // A new connection is established
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  ASSERT_TRUE(tcp_client2->write("hello1", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello1"));
  tcp_client2->close();

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

// Test that with HTTP1 we have no connection with upstream close.
TEST_P(TcpTunnelingIntegrationTest, H1UpstreamCloseNoConnectionReuse) {
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP2) {
    return;
  }
  initialize();

  // Establish a connection.
  IntegrationTcpClientPtr tcp_client1 = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send data in both directions.
  ASSERT_TRUE(tcp_client1->write("hello1", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello1"));

  // Send data from upstream to downstream and close the connection
  // from the upstream.
  upstream_request_->encodeData("world1", false);
  tcp_client1->waitForData("world1");
  ASSERT_TRUE(fake_upstream_connection_->close());

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  tcp_client1->waitForHalfClose();
  tcp_client1->close();

  // Establish a new connection.
  IntegrationTcpClientPtr tcp_client2 = makeTcpConnection(lookupPort("tcp_proxy"));
  // A new connection is established
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  ASSERT_TRUE(tcp_client2->write("hello2", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello2"));
  ASSERT_TRUE(fake_upstream_connection_->close());

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  tcp_client2->waitForHalfClose();
  tcp_client2->close();
}

TEST_P(TcpTunnelingIntegrationTest, 2xxStatusCodeValidHttp1) {
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP2) {
    return;
  }
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send valid response headers, in HTTP1 all status codes in the 2xx range
  // are considered valid.
  default_response_headers_.setStatus(enumToInt(Http::Code::Accepted));
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send some data from downstream to upstream, and make sure it goes through.
  ASSERT_TRUE(tcp_client->write("hello", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Send data from upstream to downstream.
  upstream_request_->encodeData(12, false);
  ASSERT_TRUE(tcp_client->waitForData(12));

  // Close the downstream connection and wait for upstream disconnect
  tcp_client->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(TcpTunnelingIntegrationTest, ContentLengthHeaderIgnoredHttp1) {
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP2) {
    return;
  }
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send upgrade headers downstream, including content-length that must be
  // ignored.
  default_response_headers_.setStatus(enumToInt(Http::Code::IMUsed));
  default_response_headers_.setContentLength(10);
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send data from upstream to downstream.
  upstream_request_->encodeData(12, false);
  ASSERT_TRUE(tcp_client->waitForData(12));

  // Now send some data and close the TCP client.
  ASSERT_TRUE(tcp_client->write("hello", false));
  tcp_client->close();
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(TcpTunnelingIntegrationTest, TransferEncodingHeaderIgnoredHttp1) {
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP2) {
    return;
  }
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));
  // Using raw connection to be able to set Transfer-encoding header.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));
  ASSERT_THAT(data, testing::HasSubstr("CONNECT host.com:80 HTTP/1.1"));

  // Send upgrade headers downstream, fully establishing the connection.
  ASSERT_TRUE(
      fake_upstream_connection->write("HTTP/1.1 200 OK\r\nTransfer-encoding: chunked\r\n\r\n"));

  // Now send some data and close the TCP client.
  ASSERT_TRUE(tcp_client->write("hello"));
  ASSERT_TRUE(
      fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("hello")));

  // Close connections.
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client->close();
}

TEST_P(TcpTunnelingIntegrationTest, DeferTransmitDataUntilSuccessConnectResponseIsReceived) {
  if (!wait_for_connect_response_) {
    return;
  }
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send some data straight away.
  ASSERT_TRUE(tcp_client->write("hello", false));

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Wait a bit, no data should go through.
  ASSERT_FALSE(upstream_request_->waitForData(*dispatcher_, 1, std::chrono::milliseconds(100)));

  upstream_request_->encodeHeaders(default_response_headers_, false);

  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  tcp_client->close();
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    // If the upstream now sends 'end stream' the connection is fully closed.
    upstream_request_->encodeData(0, true);
  }
}

TEST_P(TcpTunnelingIntegrationTest, NoDataTransmittedIfConnectFailureResponseIsReceived) {
  if (!wait_for_connect_response_) {
    return;
  }
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send some data straight away.
  ASSERT_TRUE(tcp_client->write("hello", false));

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  default_response_headers_.setStatus(enumToInt(Http::Code::ServiceUnavailable));
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Wait a bit, no data should go through.
  ASSERT_FALSE(upstream_request_->waitForData(*dispatcher_, 1, std::chrono::milliseconds(100)));

  tcp_client->close();
  if (upstreamProtocol() == FakeHttpConnection::Type::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
}

TEST_P(TcpTunnelingIntegrationTest, UpstreamDisconnectBeforeResponseReceived) {
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  ASSERT_TRUE(fake_upstream_connection_->close());
  tcp_client->waitForHalfClose();
  tcp_client->close();
}

INSTANTIATE_TEST_SUITE_P(
    IpAndHttpVersions, TcpTunnelingIntegrationTest,
    ::testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                       testing::Values(FakeHttpConnection::Type::HTTP1,
                                       FakeHttpConnection::Type::HTTP2),
                       testing::Values(false, true)),
    TcpTunnelingIntegrationTest::paramsToString);

} // namespace
} // namespace Envoy
