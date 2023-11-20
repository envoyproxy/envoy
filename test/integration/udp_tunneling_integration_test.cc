#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/upstreams/http/udp/v3/udp_connection_pool.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Terminates CONNECT-UDP and sends raw UDP datagrams upstream.
class ConnectUdpTerminationIntegrationTest : public HttpProtocolIntegrationTest {
public:
  ConnectUdpTerminationIntegrationTest() = default;

  ~ConnectUdpTerminationIntegrationTest() override {
    // Since the upstream is a UDP server, there is nothing to check on the upstream side. Simply
    // make sure that the connection is closed to avoid TSAN error.
    if (codec_client_) {
      codec_client_->close();
    }
  }

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_delayed_close_timeout()->set_seconds(1);
          if (enable_timeout_) {
            hcm.mutable_stream_idle_timeout()->set_seconds(0);
            hcm.mutable_stream_idle_timeout()->set_nanos(200 * 1000 * 1000);
          }
          if (!host_to_match_.empty()) {
            auto* route_config = hcm.mutable_route_config();
            ASSERT_EQ(1, route_config->virtual_hosts_size());
            route_config->mutable_virtual_hosts(0)->clear_domains();
            route_config->mutable_virtual_hosts(0)->add_domains(host_to_match_);
          }
          ConfigHelper::setConnectUdpConfig(hcm, true,
                                            downstream_protocol_ == Http::CodecType::HTTP3);
        });
    setUdpFakeUpstream(FakeUpstreamConfig::UdpConfig());
    HttpIntegrationTest::initialize();
  }

  void setUpConnection() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(connect_udp_headers_);
    request_encoder_ = &encoder_decoder.first;
    response_ = std::move(encoder_decoder.second);
    response_->waitForHeaders();
  }

  void sendBidirectionalData(const std::string downstream_send_data = "hello",
                             const std::string upstream_received_data = "hello",
                             const std::string upstream_send_data = "there!",
                             const std::string downstream_received_data = "there!") {
    // Send some data upstream.
    codec_client_->sendData(*request_encoder_, downstream_send_data, false);
    Network::UdpRecvData request_datagram;
    ASSERT_TRUE(fake_upstreams_[0]->waitForUdpDatagram(request_datagram));
    EXPECT_EQ(upstream_received_data, request_datagram.buffer_->toString());

    // Send some data downstream.
    fake_upstreams_[0]->sendUdpDatagram(upstream_send_data, request_datagram.addresses_.peer_);
    response_->waitForBodyData(downstream_received_data.length());
    EXPECT_EQ(downstream_received_data, response_->body());
  }

  void exchangeValidCapsules() {
    const std::string sent_capsule_fragment =
        absl::HexStringToBytes("00"             // DATAGRAM Capsule Type
                               "08"             // Capsule Length
                               "00"             // Context ID
                               "a1a2a3a4a5a6a7" // UDP Proxying Payload
        );
    const std::string received_capsule_fragment =
        absl::HexStringToBytes("00"             // DATAGRAM Capsule Type
                               "08"             // Capsule Length
                               "00"             // Context ID
                               "b1b2b3b4b5b6b7" // UDP Proxying Payload
        );

    sendBidirectionalData(sent_capsule_fragment, absl::HexStringToBytes("a1a2a3a4a5a6a7"),
                          absl::HexStringToBytes("b1b2b3b4b5b6b7"), received_capsule_fragment);
  }

  // The Envoy HTTP/2 and HTTP/3 clients expect the request header map to be in the form of HTTP/1
  // upgrade to issue an extended CONNECT request.
  Http::TestRequestHeaderMapImpl connect_udp_headers_{
      {":method", "GET"},         {":path", "/.well-known/masque/udp/foo.lyft.com/80/"},
      {"upgrade", "connect-udp"}, {"connection", "upgrade"},
      {":scheme", "https"},       {":authority", "example.org"},
      {"capsule-protocol", "?1"}};

  IntegrationStreamDecoderPtr response_;
  bool enable_timeout_{};
  std::string host_to_match_{};
};

TEST_P(ConnectUdpTerminationIntegrationTest, ExchangeCapsules) {
  initialize();
  setUpConnection();
  exchangeValidCapsules();
}

TEST_P(ConnectUdpTerminationIntegrationTest, ExchangeCapsulesWithHostMatch) {
  host_to_match_ = "foo.lyft.com:80";
  initialize();
  setUpConnection();
  exchangeValidCapsules();
}

TEST_P(ConnectUdpTerminationIntegrationTest, IncorrectHostMatch) {
  host_to_match_ = "foo.lyft.com:80";
  connect_udp_headers_.setPath("/.well-known/masque/udp/bar.lyft.com/80/");
  initialize();
  setUpConnection();
  EXPECT_EQ("404", response_->headers().getStatusValue());
}

TEST_P(ConnectUdpTerminationIntegrationTest, IncorrectPortMatch) {
  host_to_match_ = "foo.lyft.com:80";
  connect_udp_headers_.setPath("/.well-known/masque/udp/foo.lyft.com/8080/");
  initialize();
  setUpConnection();
  EXPECT_EQ("404", response_->headers().getStatusValue());
}

TEST_P(ConnectUdpTerminationIntegrationTest, IPv4HostMatch) {
  host_to_match_ = "179.0.112.43:80";
  connect_udp_headers_.setPath("/.well-known/masque/udp/179.0.112.43/80/");
  initialize();
  setUpConnection();
}

TEST_P(ConnectUdpTerminationIntegrationTest, IPv6HostMatch) {
  host_to_match_ = "[2001:0db8:85a3::8a2e:0370:7334]:80";
  connect_udp_headers_.setPath("/.well-known/masque/udp/2001:0db8:85a3::8a2e:0370:7334/80/");
  initialize();
  setUpConnection();
}

TEST_P(ConnectUdpTerminationIntegrationTest, IPv6WithZoneIdHostMatch) {
  host_to_match_ = "[fe80::a%ee1]:80";
  connect_udp_headers_.setPath("/.well-known/masque/udp/fe80::a%25ee1/80/");
  initialize();
  setUpConnection();
  EXPECT_EQ("404", response_->headers().getStatusValue());
}

TEST_P(ConnectUdpTerminationIntegrationTest, ExchangeCapsulesWithoutCapsuleProtocolHeader) {
  initialize();
  connect_udp_headers_.remove(Envoy::Http::Headers::get().CapsuleProtocol);
  setUpConnection();
  exchangeValidCapsules();
}

TEST_P(ConnectUdpTerminationIntegrationTest, StreamIdleTimeout) {
  enable_timeout_ = true;
  initialize();
  setUpConnection();

  // Wait for the timeout to close the connection.
  ASSERT_TRUE(response_->waitForReset());
}

TEST_P(ConnectUdpTerminationIntegrationTest, MaxStreamDuration) {
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
  exchangeValidCapsules();

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_rq_max_duration_reached", 1);

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    ASSERT_TRUE(response_->waitForReset());
  }
}

TEST_P(ConnectUdpTerminationIntegrationTest, PathWithInvalidUriTemplate) {
  initialize();
  connect_udp_headers_.setPath("/masque/udp/foo.lyft.com/80/");
  setUpConnection();
  EXPECT_EQ("404", response_->headers().getStatusValue());
}

TEST_P(ConnectUdpTerminationIntegrationTest, PathWithEmptyHost) {
  initialize();
  connect_udp_headers_.setPath("/.well-known/masque/udp//80/");
  setUpConnection();
  EXPECT_EQ("404", response_->headers().getStatusValue());
}

TEST_P(ConnectUdpTerminationIntegrationTest, PathWithEmptyPort) {
  initialize();
  connect_udp_headers_.setPath("/.well-known/masque/udp/foo.lyft.com//");
  setUpConnection();
  EXPECT_EQ("404", response_->headers().getStatusValue());
}

TEST_P(ConnectUdpTerminationIntegrationTest, DropUnknownCapsules) {
  initialize();
  setUpConnection();
  Network::UdpRecvData request_datagram;
  const std::string unknown_capsule_fragment =
      absl::HexStringToBytes("01"             // DATAGRAM Capsule Type
                             "08"             // Capsule Length
                             "00"             // Context ID
                             "a1a2a3a4a5a6a7" // UDP Proxying Payload
      );
  codec_client_->sendData(*request_encoder_, unknown_capsule_fragment, false);
  ASSERT_FALSE(
      fake_upstreams_[0]->waitForUdpDatagram(request_datagram, std::chrono::milliseconds(1)));

  const std::string unknown_context_id =
      absl::HexStringToBytes("00"             // DATAGRAM Capsule Type
                             "08"             // Capsule Length
                             "01"             // Context ID
                             "a1a2a3a4a5a6a7" // UDP Proxying Payload
      );
  codec_client_->sendData(*request_encoder_, unknown_context_id, false);
  ASSERT_FALSE(
      fake_upstreams_[0]->waitForUdpDatagram(request_datagram, std::chrono::milliseconds(1)));
}

INSTANTIATE_TEST_SUITE_P(Protocols, ConnectUdpTerminationIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Forwards the CONNECT-UDP request upstream.
class ForwardingConnectUdpIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          ConfigHelper::setConnectUdpConfig(hcm, false,
                                            downstream_protocol_ == Http::CodecType::HTTP3);
        });

    HttpProtocolIntegrationTest::initialize();
  }

  // The Envoy HTTP/2 and HTTP/3 clients expect the request header map to be in the form of HTTP/1
  // upgrade to issue an extended CONNECT request.
  Http::TestRequestHeaderMapImpl connect_udp_headers_{
      {":method", "GET"},         {":path", "/.well-known/masque/udp/foo.lyft.com/80/"},
      {"upgrade", "connect-udp"}, {"connection", "upgrade"},
      {":scheme", "https"},       {":authority", "example.org"},
      {"capsule-protocol", "?1"}};

  IntegrationStreamDecoderPtr response_;
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, ForwardingConnectUdpIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3},
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3})),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(ForwardingConnectUdpIntegrationTest, ForwardConnectUdp) {
  initialize();

  // Send request headers.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(connect_udp_headers_);
  request_encoder_ = &encoder_decoder.first;
  response_ = std::move(encoder_decoder.second);

  // Wait for them to arrive upstream.
  AssertionResult result =
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Check the request header contains correct field values (Normalized to HTTP/1).
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "GET");
  EXPECT_EQ(upstream_request_->headers().getConnectionValue(), "upgrade");
  EXPECT_EQ(upstream_request_->headers().getUpgradeValue(), "connect-udp");
  EXPECT_EQ(upstream_request_->headers().getHostValue(), "foo.lyft.com:80");

  // Send response headers
  upstream_request_->encodeHeaders(default_response_headers_, false);
  // Wait for them to arrive downstream.
  response_->waitForHeaders();
  cleanupUpstreamAndDownstream();
}

TEST_P(ForwardingConnectUdpIntegrationTest, DoNotForwardNonConnectUdp) {
  initialize();

  Http::TestRequestHeaderMapImpl websocket_headers_{
      {":method", "GET"},        {":path", "/"},       {"upgrade", "websocket"},
      {"connection", "upgrade"}, {":scheme", "https"}, {":authority", "foo.lyft.com:80"}};

  // Send WebSocket request headers.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(websocket_headers_);
  request_encoder_ = &encoder_decoder.first;
  response_ = std::move(encoder_decoder.second);
  response_->waitForHeaders();

  // Envoy should return a 404 error response.
  EXPECT_EQ("404", response_->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

// Tunneling downstream UDP over an upstream HTTP CONNECT tunnel.
class UdpTunnelingIntegrationTest : public HttpProtocolIntegrationTest {
public:
  UdpTunnelingIntegrationTest()
      : HttpProtocolIntegrationTest(ConfigHelper::baseUdpListenerConfig()) {}

  struct BufferOptions {
    uint32_t max_buffered_datagrams_;
    uint32_t max_buffered_bytes_;
  };

  struct TestConfig {
    std::string proxy_host_;
    std::string target_host_;
    uint32_t max_connect_attempts_;
    uint32_t default_target_port_;
    bool use_post_;
    std::string post_path_;
    absl::optional<BufferOptions> buffer_options_;
    absl::optional<std::string> idle_timeout_;
    std::string session_access_log_config_ = "";
    std::string access_log_options_ = "";
    bool propagate_response_headers_ = false;
    bool propagate_response_trailers_ = false;
  };

  void setup(const TestConfig& config) {
    config_ = config;

    std::string filter_config =
        fmt::format(R"EOF(
name: udp_proxy
typed_config:
  '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.UdpProxyConfig
  stat_prefix: foo
  matcher:
    on_no_match:
      action:
        name: route
        typed_config:
          '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.v3.Route
          cluster: cluster_0
  session_filters:
  - name: http_capsule
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.filters.udp.udp_proxy.session.http_capsule.v3.FilterConfig
  tunneling_config:
    proxy_host: {}
    target_host: {}
    default_target_port: {}
    retry_options:
      max_connect_attempts: {}
    propagate_response_headers: {}
    propagate_response_trailers: {}
)EOF",
                    config.proxy_host_, config.target_host_, config.default_target_port_,
                    config.max_connect_attempts_, config.propagate_response_headers_,
                    config.propagate_response_trailers_);

    if (config.buffer_options_.has_value()) {
      filter_config += fmt::format(R"EOF(
    buffer_options:
      max_buffered_datagrams: {}
      max_buffered_bytes: {}
)EOF",
                                   config.buffer_options_.value().max_buffered_datagrams_,
                                   config.buffer_options_.value().max_buffered_bytes_);
    }

    if (config.use_post_) {
      filter_config += fmt::format(R"EOF(
    use_post: true
    post_path: {}
)EOF",
                                   config.post_path_);
    }

    if (config.idle_timeout_.has_value()) {
      filter_config += fmt::format(R"EOF(
  idle_timeout: {}
)EOF",
                                   config.idle_timeout_.value());
    }

    filter_config += config.session_access_log_config_ + config.access_log_options_;

    config_helper_.renameListener("udp_proxy");
    config_helper_.addConfigModifier(
        [filter_config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto* filter = listener->add_listener_filters();
          TestUtility::loadFromYaml(filter_config, *filter);
        });

    HttpIntegrationTest::initialize();

    const uint32_t port = lookupPort("udp_proxy");
    listener_address_ = Network::Utility::resolveUrl(
        fmt::format("tcp://{}:{}", Network::Test::getLoopbackAddressUrlString(version_), port));

    client_ = std::make_unique<Network::Test::UdpSyncPeer>(version_);
  };

  const std::string encapsulate(std::string datagram) {
    uint8_t capsule_length = datagram.length() + 1;
    return absl::HexStringToBytes(absl::StrCat("00", Hex::encode(&capsule_length, 1), "00")) +
           datagram;
  }

  const std::string expectedCapsules(std::vector<std::string> raw_datagrams) {
    std::string expected_capsules = "";
    for (std::string datagram : raw_datagrams) {
      expected_capsules += encapsulate(datagram);
    }

    return expected_capsules;
  }

  void expectPostRequestHeaders(const Http::RequestHeaderMap& headers) {
    EXPECT_EQ(headers.getMethodValue(), "POST");
    EXPECT_EQ(headers.getPathValue(), config_.post_path_);
  }

  void expectConnectRequestHeaders(const Http::RequestHeaderMap& original_headers) {
    Http::TestRequestHeaderMapImpl headers(original_headers);

    // For connect-udp case, the server codec will transform the H2 headers to H1. For test
    // convenience, transforming the H1 headers back to H2.
    Http::Utility::transformUpgradeRequestFromH1toH2(headers);
    std::string expected_path = absl::StrCat("/.well-known/masque/udp/", config_.target_host_, "/",
                                             config_.default_target_port_, "/");

    EXPECT_EQ(headers.getMethodValue(), "CONNECT");
    EXPECT_EQ(headers.getPathValue(), expected_path);
  }

  void expectRequestHeaders(const Http::RequestHeaderMap& headers) {
    if (config_.use_post_) {
      expectPostRequestHeaders(headers);
    } else {
      expectConnectRequestHeaders(headers);
    }
  }

  void establishConnection(const std::string initial_datagram, int tunnels_count = 1) {
    // Initial datagram will create a session and a tunnel request.
    client_->write(initial_datagram, *listener_address_);

    if (!fake_upstream_connection_) {
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    }

    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
    expectRequestHeaders(upstream_request_->headers());

    // Send upgrade headers downstream, fully establishing the connection.
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"capsule-protocol", "?1"}};
    upstream_request_->encodeHeaders(response_headers, false);

    test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_success", tunnels_count);
    test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 1);
  }

  void sendCapsuleDownstream(const std::string datagram, bool end_stream = false) {
    // Send data from upstream to downstream.
    upstream_request_->encodeData(encapsulate(datagram), end_stream);
    Network::UdpRecvData response_datagram;
    client_->recv(response_datagram);
    EXPECT_EQ(datagram, response_datagram.buffer_->toString());
  }

  TestConfig config_;
  Network::Address::InstanceConstSharedPtr listener_address_;
  std::unique_ptr<Network::Test::UdpSyncPeer> client_;
};

TEST_P(UdpTunnelingIntegrationTest, BasicFlowWithBuffering) {
  TestConfig config{"host.com",           "target.com", 1, 30, false, "",
                    BufferOptions{1, 30}, absl::nullopt};
  setup(config);

  const std::string datagram1 = "hello";
  establishConnection(datagram1);
  // Wait for buffered datagram.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({datagram1})));

  const std::string datagram2 = "hello2";
  client_->write(datagram2, *listener_address_);
  ASSERT_TRUE(
      upstream_request_->waitForData(*dispatcher_, expectedCapsules({datagram1, datagram2})));

  sendCapsuleDownstream("response1", false);
  sendCapsuleDownstream("response2", true);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

TEST_P(UdpTunnelingIntegrationTest, BasicFlowNoBuffering) {
  TestConfig config{"host.com", "target.com", 1, 30, false, "", absl::nullopt, absl::nullopt};
  setup(config);

  establishConnection("hello");
  // Since there's no buffering, the first datagram is dropped. Send another datagram and expect
  // that it's the only datagram received upstream.
  const std::string datagram2 = "hello2";
  client_->write(datagram2, *listener_address_);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({datagram2})));

  sendCapsuleDownstream("response", true);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

TEST_P(UdpTunnelingIntegrationTest, BasicFlowWithPost) {
  TestConfig config{"host.com",           "target.com", 1, 30, true, "/post/path",
                    BufferOptions{1, 30}, absl::nullopt};
  setup(config);

  const std::string datagram1 = "hello";
  establishConnection(datagram1);
  // Wait for buffered datagram.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({datagram1})));
  sendCapsuleDownstream("response", true);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

TEST_P(UdpTunnelingIntegrationTest, TwoConsecutiveDownstreamSessions) {
  TestConfig config{"host.com",           "target.com", 1, 30, false, "",
                    BufferOptions{1, 30}, absl::nullopt};
  setup(config);

  establishConnection("hello1");
  sendCapsuleDownstream("response2", true); // Will end first session.
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
  establishConnection("hello2", 2); // Will create another session.
  sendCapsuleDownstream("response2", true);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

TEST_P(UdpTunnelingIntegrationTest, IdleTimeout) {
  TestConfig config{"host.com", "target.com", 1, 30, false, "", BufferOptions{1, 30}, "0.5s"};
  setup(config);

  const std::string datagram = "hello";
  establishConnection(datagram);
  // Wait for buffered datagram.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({datagram})));

  sendCapsuleDownstream("response1", false);
  test_server_->waitForCounterEq("udp.foo.idle_timeout", 1);
  ASSERT_TRUE(upstream_request_->waitForReset());
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

TEST_P(UdpTunnelingIntegrationTest, BufferOverflowDueToCapacity) {
  TestConfig config{"host.com",           "target.com", 1, 30, false, "",
                    BufferOptions{1, 30}, absl::nullopt};
  setup(config);

  // Send two datagrams before the upstream is established. Since the buffer capacity is 1 datagram,
  // we expect the second one to be dropped.
  client_->write("hello1", *listener_address_);
  client_->write("hello2", *listener_address_);
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_buffer_overflow", 1);

  // "hello3" will drop because it's sent before the tunnel is established, and the buffer is full.
  establishConnection("hello3");
  // Wait for the buffered datagram.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({"hello1"})));
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_buffer_overflow", 2);

  sendCapsuleDownstream("response", true);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

TEST_P(UdpTunnelingIntegrationTest, BufferOverflowDueToSize) {
  TestConfig config{"host.com",   "target.com", 1, 30, false, "", BufferOptions{100, 15},
                    absl::nullopt};
  setup(config);

  // Send two datagrams before the upstream is established. Since the buffer capacity is 6 bytes,
  // we expect the second one to be dropped.
  client_->write("hello1", *listener_address_);
  client_->write("hello2", *listener_address_);
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_buffer_overflow", 1);

  // "hello3" will drop because it's sent before the tunnel is established, and the buffer is full.
  establishConnection("hello3");
  // Wait for the buffered datagram.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({"hello1"})));
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_buffer_overflow", 2);

  sendCapsuleDownstream("response", true);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

TEST_P(UdpTunnelingIntegrationTest, ConnectionReuse) {
  TestConfig config{"host.com",   "target.com", 1, 30, false, "", BufferOptions{100, 300},
                    absl::nullopt};
  setup(config);

  // Establish connection for first session.
  establishConnection("hello_1");

  // Establish connection for second session.
  Network::Test::UdpSyncPeer client2(version_);
  client2.write("hello_2", *listener_address_);

  FakeStreamPtr upstream_request2;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request2));
  ASSERT_TRUE(upstream_request2->waitForHeadersComplete());
  expectRequestHeaders(upstream_request2->headers());

  // Send upgrade headers downstream, fully establishing the connection.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"capsule-protocol", "?1"}};
  upstream_request2->encodeHeaders(response_headers, false);

  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_success", 2);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 2);

  // Wait for buffered datagram for each stream.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({"hello_1"})));
  ASSERT_TRUE(upstream_request2->waitForData(*dispatcher_, expectedCapsules({"hello_2"})));

  // Send capsule from upstream over the first stream, and close it.
  sendCapsuleDownstream("response_1", true);
  // First stream is closed so we expect active sessions to decrease to 1.
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 1);

  // Send capsule from upstream over the second stream, and close it.
  upstream_request2->encodeData(encapsulate("response_2"), true);
  Network::UdpRecvData response_datagram;
  client2.recv(response_datagram);
  EXPECT_EQ("response_2", response_datagram.buffer_->toString());
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

TEST_P(UdpTunnelingIntegrationTest, FailureOnBadResponseHeaders) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

  const std::string session_access_log_config = fmt::format(R"EOF(
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: {}
      log_format:
        text_format_source:
          inline_string: "%UPSTREAM_REQUEST_ATTEMPT_COUNT% %RESPONSE_FLAGS%\n"
)EOF",
                                                            access_log_filename);

  const TestConfig config{"host.com",
                          "target.com",
                          1,
                          30,
                          false,
                          "",
                          BufferOptions{1, 30},
                          absl::nullopt,
                          session_access_log_config};
  setup(config);

  // Initial datagram will create a session and a tunnel request.
  client_->write("hello", *listener_address_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  expectRequestHeaders(upstream_request_->headers());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "404"}};
  upstream_request_->encodeHeaders(response_headers, true);

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_connect_attempts_exceeded", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_failure", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_success", 0);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);

  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr("1 UF,URX"));
}

TEST_P(UdpTunnelingIntegrationTest, ConnectionAttemptRetry) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

  const std::string session_access_log_config = fmt::format(R"EOF(
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: {}
      log_format:
        text_format_source:
          inline_string: "%UPSTREAM_REQUEST_ATTEMPT_COUNT% %RESPONSE_FLAGS%\n"
)EOF",
                                                            access_log_filename);

  const TestConfig config{"host.com",
                          "target.com",
                          2,
                          30,
                          false,
                          "",
                          BufferOptions{1, 30},
                          absl::nullopt,
                          session_access_log_config};
  setup(config);

  // Initial datagram will create a session and a tunnel request.
  client_->write("hello", *listener_address_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  expectRequestHeaders(upstream_request_->headers());

  Http::TestResponseHeaderMapImpl fail_response_headers{{":status", "404"}};
  upstream_request_->encodeHeaders(fail_response_headers, true);

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_retry", 1);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 1);

  // The request is retried, expect new downstream headers
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  expectRequestHeaders(upstream_request_->headers());

  // Send upgrade headers downstream, fully establishing the connection.
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}, {"capsule-protocol", "?1"}};
  upstream_request_->encodeHeaders(response_headers, false);

  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_success", 1);
  sendCapsuleDownstream("response", true);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);

  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr("2 UF"));
}

TEST_P(UdpTunnelingIntegrationTest, PropagateValidResponseHeaders) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

  const std::string session_access_log_config = fmt::format(R"EOF(
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: {}
      log_format:
        text_format_source:
          inline_string: "%FILTER_STATE(envoy.udp_proxy.propagate_response_headers:TYPED)%\n"
)EOF",
                                                            access_log_filename);

  const TestConfig config{"host.com",
                          "target.com",
                          1,
                          30,
                          false,
                          "",
                          BufferOptions{1, 30},
                          absl::nullopt,
                          session_access_log_config,
                          "",
                          true,
                          false};
  setup(config);

  const std::string datagram = "hello";
  establishConnection(datagram);

  // Wait for buffered datagram.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({datagram})));

  sendCapsuleDownstream("response", true);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);

  // Verify response header value is in the access log.
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr("capsule-protocol"));
}

TEST_P(UdpTunnelingIntegrationTest, PropagateInvalidResponseHeaders) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

  const std::string session_access_log_config = fmt::format(R"EOF(
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: {}
      log_format:
        text_format_source:
          inline_string: "%FILTER_STATE(envoy.udp_proxy.propagate_response_headers:TYPED)%\n"
)EOF",
                                                            access_log_filename);

  const TestConfig config{"host.com",
                          "target.com",
                          1,
                          30,
                          false,
                          "",
                          BufferOptions{1, 30},
                          absl::nullopt,
                          session_access_log_config,
                          "",
                          true,
                          false};
  setup(config);

  client_->write("hello", *listener_address_);
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  expectRequestHeaders(upstream_request_->headers());

  Http::TestResponseHeaderMapImpl response_headers{{":status", "404"}};
  upstream_request_->encodeHeaders(response_headers, true);

  test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_connect_attempts_exceeded", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_failure", 1);
  test_server_->waitForCounterEq("cluster.cluster_0.udp.sess_tunnel_success", 0);
  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);

  // Verify response header value is in the access log.
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr("404"));
}

TEST_P(UdpTunnelingIntegrationTest, PropagateResponseTrailers) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

  const std::string session_access_log_config = fmt::format(R"EOF(
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: {}
      log_format:
        text_format_source:
          inline_string: "%FILTER_STATE(envoy.udp_proxy.propagate_response_trailers:TYPED)%\n"
)EOF",
                                                            access_log_filename);

  const TestConfig config{"host.com",
                          "target.com",
                          1,
                          30,
                          false,
                          "",
                          BufferOptions{1, 30},
                          absl::nullopt,
                          session_access_log_config,
                          "",
                          false,
                          true};
  setup(config);

  const std::string datagram = "hello";
  establishConnection(datagram);

  // Wait for buffered datagram.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({datagram})));
  sendCapsuleDownstream("response", false);

  const std::string trailer_value = "test-trailer-value";
  Http::TestResponseTrailerMapImpl response_trailers{{"test-trailer-name", trailer_value}};
  upstream_request_->encodeTrailers(response_trailers);

  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);

  // Verify response trailer value is in the access log.
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr(trailer_value));
}

TEST_P(UdpTunnelingIntegrationTest, FlushAccessLogOnTunnelConnected) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

  const std::string session_access_log_config = fmt::format(R"EOF(
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: {}
      log_format:
        text_format_source:
          inline_string: "%ACCESS_LOG_TYPE%\n"
)EOF",
                                                            access_log_filename);

  const std::string access_log_options = R"EOF(
  access_log_options:
    flush_access_log_on_tunnel_connected: true
)EOF";

  const TestConfig config{"host.com",
                          "target.com",
                          1,
                          30,
                          false,
                          "",
                          BufferOptions{1, 30},
                          absl::nullopt,
                          session_access_log_config,
                          access_log_options};
  setup(config);

  const std::string datagram = "hello";
  establishConnection(datagram);
  EXPECT_THAT(
      waitForAccessLog(access_log_filename),
      testing::HasSubstr(AccessLogType_Name(AccessLog::AccessLogType::UdpTunnelUpstreamConnected)));

  // Wait for buffered datagram.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({datagram})));
  sendCapsuleDownstream("response", true);

  test_server_->waitForGaugeEq("udp.foo.downstream_sess_active", 0);
}

TEST_P(UdpTunnelingIntegrationTest, FlushAccessLogPeriodically) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

  const std::string session_access_log_config = fmt::format(R"EOF(
  access_log:
  - name: envoy.access_loggers.file
    typed_config:
      '@type': type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: {}
      log_format:
        text_format_source:
          inline_string: "%ACCESS_LOG_TYPE%\n"
)EOF",
                                                            access_log_filename);
  const std::string access_log_options = R"EOF(
  access_log_options:
    access_log_flush_interval: 0.5s
)EOF";

  const TestConfig config{"host.com",
                          "target.com",
                          1,
                          30,
                          false,
                          "",
                          BufferOptions{1, 30},
                          absl::nullopt,
                          session_access_log_config,
                          access_log_options};
  setup(config);

  const std::string datagram = "hello";
  establishConnection(datagram);
  // Wait for buffered datagram.
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, expectedCapsules({datagram})));

  sendCapsuleDownstream("response", false);
  EXPECT_THAT(waitForAccessLog(access_log_filename),
              testing::HasSubstr(AccessLogType_Name(AccessLog::AccessLogType::UdpPeriodic)));
}

INSTANTIATE_TEST_SUITE_P(IpAndHttpVersions, UdpTunnelingIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2}, {Http::CodecType::HTTP2})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

} // namespace
} // namespace Envoy
