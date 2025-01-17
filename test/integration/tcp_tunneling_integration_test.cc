#include <chrono>
#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/upstreams/http/tcp/v3/tcp_connection_pool.pb.h"

#include "test/integration/filters/add_header_filter.pb.h"
#include "test/integration/filters/stop_and_continue_filter_config.pb.h"
#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"
#include "test/integration/tcp_tunneling_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Terminating CONNECT and sending raw TCP upstream.
class ConnectTerminationIntegrationTest : public HttpProtocolIntegrationTest {
public:
  ConnectTerminationIntegrationTest() { enableHalfClose(true); }

  void initialize() override {
    useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
                 "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED% %ACCESS_LOG_TYPE%");
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_delayed_close_timeout()->set_seconds(1);
          ConfigHelper::setConnectConfig(hcm, !terminate_via_cluster_config_, allow_post_,
                                         downstream_protocol_ == Http::CodecType::HTTP3);

          if (enable_timeout_) {
            hcm.mutable_stream_idle_timeout()->set_seconds(0);
            hcm.mutable_stream_idle_timeout()->set_nanos(200 * 1000 * 1000);
          }
          if (exact_match_) {
            auto* route_config = hcm.mutable_route_config();
            ASSERT_EQ(1, route_config->virtual_hosts_size());
            route_config->mutable_virtual_hosts(0)->clear_domains();
            route_config->mutable_virtual_hosts(0)->add_domains("foo.lyft.com:80");
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
                                                  {":authority", "foo.lyft.com:80"}};

  void sendBidirectionalDataAndCleanShutdown() {
    sendBidirectionalData("hello", "hello", "there!", "there!");
    // Send a second set of data to make sure for example headers are only sent once.
    sendBidirectionalData(",bye", "hello,bye", "ack", "there!ack");

    // Send an end stream. This should result in half close upstream.
    codec_client_->sendData(*request_encoder_, "", true);
    ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());

    // Now send a FIN from upstream. This should result in clean shutdown downstream.
    ASSERT_TRUE(fake_raw_upstream_connection_->close());
    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
    } else {
      ASSERT_TRUE(response_->waitForEndStream());
      ASSERT_FALSE(response_->reset());
    }

    // expected_header_bytes_* is 0 as we do not use proxy protocol.
    // expected_wire_bytes_sent is 9 as the Envoy sends "hello,bye".
    // expected_wire_bytes_received is 9 as the Upstream sends "there!ack".
    const int expected_wire_bytes_sent = 9;
    const int expected_wire_bytes_received = 9;
    const int expected_header_bytes_sent = 0;
    const int expected_header_bytes_received = 0;
    checkAccessLogOutput(expected_wire_bytes_sent, expected_wire_bytes_received,
                         expected_header_bytes_sent, expected_header_bytes_received);
    ++access_log_entry_;
  }

  void checkAccessLogOutput(int expected_wire_bytes_sent, int expected_wire_bytes_received,
                            int expected_header_bytes_sent, int expected_header_bytes_received) {
    std::string log = waitForAccessLog(access_log_name_, access_log_entry_);
    std::vector<std::string> log_entries = absl::StrSplit(log, ' ');
    const int wire_bytes_sent = std::stoi(log_entries[0]),
              wire_bytes_received = std::stoi(log_entries[1]),
              header_bytes_sent = std::stoi(log_entries[2]),
              header_bytes_received = std::stoi(log_entries[3]);
    EXPECT_EQ(wire_bytes_sent, expected_wire_bytes_sent);
    EXPECT_EQ(wire_bytes_received, expected_wire_bytes_received);
    EXPECT_EQ(header_bytes_sent, expected_header_bytes_sent);
    EXPECT_EQ(header_bytes_received, expected_header_bytes_received);
  }

  FakeRawConnectionPtr fake_raw_upstream_connection_;
  IntegrationStreamDecoderPtr response_;
  bool terminate_via_cluster_config_{};
  bool enable_timeout_{};
  bool exact_match_{};
  bool allow_post_{};
  uint32_t access_log_entry_{0};
};

// Verify that H/2 extended CONNECT with bytestream protocol is treated like
// standard CONNECT request
TEST_P(ConnectTerminationIntegrationTest, ExtendedConnectWithBytestreamProtocol) {
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    // Extended CONNECT is applicable to H/2 and H/3 protocols only
    return;
  }
  initialize();

  connect_headers_.clear();
  // The client H/2 codec expects the request header map to be in the form of H/1
  // upgrade to issue an extended CONNECT request
  connect_headers_.copyFrom(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/"},
                                                           {"upgrade", "bytestream"},
                                                           {"connection", "upgrade"},
                                                           {":scheme", "https"},
                                                           {":authority", "foo.lyft.com:80"}});

  setUpConnection();
  sendBidirectionalDataAndCleanShutdown();
}

TEST_P(ConnectTerminationIntegrationTest, Basic) {
  // Regression test upstream connection establishment before connect termination.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();
    for (int i = 0; i < static_resources->clusters_size(); ++i) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(i);
      cluster->mutable_preconnect_policy()->mutable_per_upstream_preconnect_ratio()->set_value(1.5);
    }
  });

  initialize();

  setUpConnection();
  sendBidirectionalDataAndCleanShutdown();
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_total", 2);
  cleanupUpstreamAndDownstream();

  setUpConnection();
  sendBidirectionalDataAndCleanShutdown();
}

TEST_P(ConnectTerminationIntegrationTest, ManyStreams) {
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    // Resetting an individual stream requires HTTP/2 or later.
    return;
  }
  autonomous_upstream_ = true; // Sending raw HTTP/1.1
  setUpstreamProtocol(Http::CodecType::HTTP1);
  initialize();

  auto upstream = reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get());
  upstream->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}, {"content-length", "0"}})));
  upstream->setResponseBody("");
  std::string response_body = "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n";
  codec_client_ = makeHttpConnection(lookupPort("http"));

  std::vector<Http::RequestEncoder*> encoders;
  std::vector<IntegrationStreamDecoderPtr> responses;
  const int num_loops = 50;

  // Do 2x loops and reset half the streams to fuzz lifetime issues
  for (int i = 0; i < num_loops * 2; ++i) {
    auto encoder_decoder = codec_client_->startRequest(connect_headers_);
    if (i % 2 == 0) {
      codec_client_->sendReset(encoder_decoder.first);
    } else {
      encoders.push_back(&encoder_decoder.first);
      responses.push_back(std::move(encoder_decoder.second));
    }
  }

  // Finish up the non-reset streams. The autonomous upstream will send the response.
  for (int i = 0; i < num_loops; ++i) {
    // Send some data upstream.
    codec_client_->sendData(*encoders[i], "GET / HTTP/1.1\r\nHost: www.google.com\r\n\r\n", false);
    responses[i]->waitForBodyData(response_body.length());
    EXPECT_EQ(response_body, responses[i]->body());
  }

  codec_client_->close();
}

TEST_P(ConnectTerminationIntegrationTest, LogOnSuccessfulTunnel) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.mutable_access_log_options()->set_flush_log_on_tunnel_successfully_established(true);
      });

  initialize();

  setUpConnection();
  std::string log = waitForAccessLog(access_log_name_, access_log_entry_);
  EXPECT_THAT(log, testing::HasSubstr("DownstreamTunnelSuccessfullyEstablished"));
  ++access_log_entry_;
  sendBidirectionalDataAndCleanShutdown();
}

TEST_P(ConnectTerminationIntegrationTest, BasicWithClusterconfig) {
  terminate_via_cluster_config_ = true;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* upgrade =
        bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_upstream_config();
    envoy::extensions::upstreams::http::tcp::v3::TcpConnectionPoolProto tcp_config;
    upgrade->set_name("envoy.filters.connection_pools.http.tcp");
    upgrade->mutable_typed_config()->PackFrom(tcp_config);
  });

  initialize();

  setUpConnection();
  sendBidirectionalDataAndCleanShutdown();
}

TEST_P(ConnectTerminationIntegrationTest, BasicAllowPost) {
  // This case does not handle delay close well.
  allow_post_ = true;
  initialize();

  // Use POST request.
  connect_headers_.setMethod("POST");
  connect_headers_.setPath("/");
  connect_headers_.setScheme("https");

  setUpConnection();
  sendBidirectionalDataAndCleanShutdown();
}

TEST_P(ConnectTerminationIntegrationTest, UsingHostMatch) {
  exact_match_ = true;
  initialize();

  setUpConnection();
  sendBidirectionalDataAndCleanShutdown();
}

TEST_P(ConnectTerminationIntegrationTest, DownstreamClose) {
  initialize();

  setUpConnection();
  sendBidirectionalData();

  // Tear down by closing the client connection.
  codec_client_->close();
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());
  const int expected_wire_bytes_sent = 5;     // Envoy sends "hello".
  const int expected_wire_bytes_received = 6; // Upstream sends "there!".
  // expected_header_bytes_* is 0 as we do not use proxy protocol.
  const int expected_header_bytes_sent = 0;
  const int expected_header_bytes_received = 0;
  checkAccessLogOutput(expected_wire_bytes_sent, expected_wire_bytes_received,
                       expected_header_bytes_sent, expected_header_bytes_received);
}

TEST_P(ConnectTerminationIntegrationTest, DownstreamReset) {
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    // Resetting an individual stream requires HTTP/2 or later.
    return;
  }
  initialize();

  setUpConnection();
  sendBidirectionalData();

  // Tear down by resetting the client stream.
  codec_client_->sendReset(*request_encoder_);
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());

  const int expected_wire_bytes_sent = 5;     // Envoy sends "hello".
  const int expected_wire_bytes_received = 6; // Upstream sends "there!".
  // expected_header_bytes_* is 0 as we do not use proxy protocol.
  const int expected_header_bytes_sent = 0;
  const int expected_header_bytes_received = 0;
  checkAccessLogOutput(expected_wire_bytes_sent, expected_wire_bytes_received,
                       expected_header_bytes_sent, expected_header_bytes_received);
}

TEST_P(ConnectTerminationIntegrationTest, UpstreamClose) {
  initialize();

  setUpConnection();
  sendBidirectionalData();

  // Tear down by closing the upstream connection.
  ASSERT_TRUE(fake_raw_upstream_connection_->close());
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    // In HTTP/3 end stream will be sent when the upstream connection is closed, and
    // STOP_SENDING frame sent instead of reset.
    ASSERT_TRUE(response_->waitForEndStream());
    ASSERT_TRUE(response_->waitForReset());
  } else if (downstream_protocol_ == Http::CodecType::HTTP2) {
    ASSERT_TRUE(response_->waitForReset());
  } else {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  }
  const int expected_wire_bytes_sent = 5;     // Envoy sends "hello".
  const int expected_wire_bytes_received = 6; // Upstream sends "there!".
  // expected_header_bytes_* is 0 as we do not use proxy protocol.
  const int expected_header_bytes_sent = 0;
  const int expected_header_bytes_received = 0;
  checkAccessLogOutput(expected_wire_bytes_sent, expected_wire_bytes_received,
                       expected_header_bytes_sent, expected_header_bytes_received);
}

TEST_P(ConnectTerminationIntegrationTest, UpstreamCloseWithHalfCloseEnabled) {
  // When allow_multiplexed_upstream_half_close is enabled router and filter
  // manager do not reset streams where upstream ended before downstream.
  // Verify that stream with TCP proxy upstream is closed as soon as the upstream TCP
  // connection is half closed even if the allow_multiplexed_upstream_half_close is enabled.
  // In this case the stream is reset in the TcpUpstream::onUpstreamData
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.allow_multiplexed_upstream_half_close", "true");
  initialize();

  setUpConnection();
  sendBidirectionalData();

  // Tear down by closing the upstream connection.
  ASSERT_TRUE(fake_raw_upstream_connection_->close());
  if (downstream_protocol_ == Http::CodecType::HTTP3) {
    // In HTTP/3 end stream will be sent when the upstream connection is closed, and
    // STOP_SENDING frame sent instead of reset.
    ASSERT_TRUE(response_->waitForEndStream());
    ASSERT_TRUE(response_->waitForReset());
  } else if (downstream_protocol_ == Http::CodecType::HTTP2) {
    ASSERT_TRUE(response_->waitForReset());
  } else {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  }
  const int expected_wire_bytes_sent = 5;     // Envoy sends "hello".
  const int expected_wire_bytes_received = 6; // Upstream sends "there!".
  // expected_header_bytes_* is 0 as we do not use proxy protocol.
  const int expected_header_bytes_sent = 0;
  const int expected_header_bytes_received = 0;
  checkAccessLogOutput(expected_wire_bytes_sent, expected_wire_bytes_received,
                       expected_header_bytes_sent, expected_header_bytes_received);
}

TEST_P(ConnectTerminationIntegrationTest, TestTimeout) {
  enable_timeout_ = true;
  initialize();

  setUpConnection();

  // Wait for the timeout to close the connection.
  ASSERT_TRUE(response_->waitForReset());
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());
}

TEST_P(ConnectTerminationIntegrationTest, BuggyHeaders) {
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    return;
  }
  initialize();

  // Sending a header-only request is probably buggy, but rather than having a
  // special corner case it is treated as a regular half close.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  response_ = codec_client_->makeHeaderOnlyRequest(connect_headers_);
  // If the connection is established (created, set to half close, and then the
  // FIN arrives), make sure the FIN arrives, and send a FIN from upstream.
  if (fake_upstreams_[0]->waitForRawConnection(fake_raw_upstream_connection_) &&
      fake_raw_upstream_connection_->connected()) {
    ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());
    ASSERT_TRUE(fake_raw_upstream_connection_->close());
  }

  // Either with early close, or half close, the FIN from upstream should result
  // in clean stream teardown.
  ASSERT_TRUE(response_->waitForEndStream());
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

  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    ASSERT_TRUE(codec_client_->waitForDisconnect());
  } else {
    ASSERT_TRUE(response_->waitForReset());
    codec_client_->close();
  }
}

// Verify Envoy ignores the Host field in HTTP/1.1 CONNECT message.
TEST_P(ConnectTerminationIntegrationTest, IgnoreH11HostField) {
  // This test is HTTP/1.1 specific.
  if (downstream_protocol_ != Http::CodecType::HTTP1) {
    return;
  }
  initialize();

  std::string response;
  const std::string full_request = "CONNECT www.foo.com:443 HTTP/1.1\r\n"
                                   "Host: www.bar.com:443\r\n\r\n";
  EXPECT_LOG_CONTAINS(
      "",
      "':authority', 'www.foo.com:443'\n"
      "':method', 'CONNECT'",
      sendRawHttpAndWaitForResponse(lookupPort("http"), full_request.c_str(), &response, true););
}

INSTANTIATE_TEST_SUITE_P(HttpAndIpVersions, ConnectTerminationIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

using Params = std::tuple<Network::Address::IpVersion, Http::CodecType>;

// Test with proxy protocol headers.
class ProxyProtocolConnectTerminationIntegrationTest : public ConnectTerminationIntegrationTest {
protected:
  void initialize() override {
    useAccessLog("%UPSTREAM_WIRE_BYTES_SENT% %UPSTREAM_WIRE_BYTES_RECEIVED% "
                 "%UPSTREAM_HEADER_BYTES_SENT% %UPSTREAM_HEADER_BYTES_RECEIVED%");
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_delayed_close_timeout()->set_seconds(1);
          ConfigHelper::setConnectConfig(hcm, !terminate_via_cluster_config_, allow_post_,
                                         downstream_protocol_ == Http::CodecType::HTTP3,
                                         proxy_protocol_version_);
        });
    HttpIntegrationTest::initialize();
  }

  envoy::config::core::v3::ProxyProtocolConfig::Version proxy_protocol_version_{
      envoy::config::core::v3::ProxyProtocolConfig::V1};

  void sendBidirectionalDataAndCleanShutdownWithProxyProtocol() {
    sendBidirectionalData("hello", "hello", "there!", "there!");
    // Send a second set of data to make sure for example headers are only sent once.
    sendBidirectionalData(",bye", "hello,bye", "ack", "there!ack");

    // Send an end stream. This should result in half close upstream.
    codec_client_->sendData(*request_encoder_, "", true);
    ASSERT_TRUE(fake_raw_upstream_connection_->waitForHalfClose());

    // Now send a FIN from upstream. This should result in clean shutdown downstream.
    ASSERT_TRUE(fake_raw_upstream_connection_->close());
    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      ASSERT_TRUE(codec_client_->waitForDisconnect());
    } else {
      ASSERT_TRUE(response_->waitForEndStream());
      ASSERT_FALSE(response_->reset());
    }
    const bool is_ipv4 = version_ == Network::Address::IpVersion::v4;

    // expected_header_bytes_sent is based on the proxy protocol header sent to
    //  the upstream.
    // expected_header_bytes_received is zero since Envoy initiates the proxy
    // protocol.
    // expected_wire_bytes_sent changes depending on the proxy protocol header
    // size which varies based on ip version. Envoy also sends "hello,bye".
    // expected_wire_bytes_received is 9 as the Upstream sends "there!ack".
    int expected_wire_bytes_sent;
    int expected_header_bytes_sent;
    if (proxy_protocol_version_ == envoy::config::core::v3::ProxyProtocolConfig::V1) {
      expected_wire_bytes_sent = is_ipv4 ? 53 : 41;
      expected_header_bytes_sent = is_ipv4 ? 44 : 32;
    } else {
      expected_wire_bytes_sent = is_ipv4 ? 37 : 61;
      expected_header_bytes_sent = is_ipv4 ? 28 : 52;
    }
    const int expected_wire_bytes_received = 9;
    const int expected_header_bytes_received = 0;
    checkAccessLogOutput(expected_wire_bytes_sent, expected_wire_bytes_received,
                         expected_header_bytes_sent, expected_header_bytes_received);
  }
};

TEST_P(ProxyProtocolConnectTerminationIntegrationTest, SendsProxyProtoHeadersv1) {
  initialize();

  setUpConnection();
  sendBidirectionalDataAndCleanShutdownWithProxyProtocol();
}

TEST_P(ProxyProtocolConnectTerminationIntegrationTest, SendsProxyProtoHeadersv2) {
  proxy_protocol_version_ = envoy::config::core::v3::ProxyProtocolConfig::V2;
  initialize();

  setUpConnection();
  sendBidirectionalDataAndCleanShutdownWithProxyProtocol();
}

INSTANTIATE_TEST_SUITE_P(HttpAndIpVersions, ProxyProtocolConnectTerminationIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// For this class, forward the CONNECT request upstream
class ProxyingConnectIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          ConfigHelper::setConnectConfig(hcm, false, false,
                                         downstream_protocol_ == Http::CodecType::HTTP3);

          if (add_upgrade_config_) {
            auto* route_config = hcm.mutable_route_config();
            ASSERT_EQ(1, route_config->virtual_hosts_size());
            auto* route = route_config->mutable_virtual_hosts(0)->add_routes();
            route->mutable_match()->set_prefix("/");
            route->mutable_route()->set_cluster("cluster_0");
            hcm.add_upgrade_configs()->set_upgrade_type("websocket");
          }
        });

    HttpProtocolIntegrationTest::initialize();
  }

  Http::TestRequestHeaderMapImpl connect_headers_{{":method", "CONNECT"},
                                                  {":authority", "foo.lyft.com:80"}};

  IntegrationStreamDecoderPtr response_;
  bool add_upgrade_config_{false};
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, ProxyingConnectIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3},
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3})),
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
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    EXPECT_TRUE(upstream_request_->headers().get(Http::Headers::get().Protocol).empty());
  } else {
    EXPECT_EQ("", upstream_request_->headers().getSchemeValue());
    EXPECT_EQ("", upstream_request_->headers().getProtocolValue());
    EXPECT_EQ("", upstream_request_->headers().getSchemeValue());
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

TEST_P(ProxyingConnectIntegrationTest, ProxyExtendedConnect) {
  add_upgrade_config_ = true;
  initialize();

  // Send request headers.
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // The test client H/2 or H/3 codecs expect the request header map to be in the form of H/1
  // upgrade to issue an extended CONNECT request.
  auto encoder_decoder = codec_client_->startRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {"upgrade", "websocket"},
                                     {"connection", "upgrade"},
                                     {":scheme", "https"},
                                     {":authority", "foo.lyft.com:80"}});
  request_encoder_ = &encoder_decoder.first;
  response_ = std::move(encoder_decoder.second);

  // Wait for them to arrive upstream.
  AssertionResult result =
      fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
  RELEASE_ASSERT(result, result.message());
  result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
  RELEASE_ASSERT(result, result.message());
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ("/", upstream_request_->headers().getPathValue());
  EXPECT_EQ("foo.lyft.com:80", upstream_request_->headers().getHostValue());

  // The fake upstream server codec will transform extended CONNECT to upgrade headers
  EXPECT_EQ("GET", upstream_request_->headers().getMethodValue());
  EXPECT_TRUE(upstream_request_->headers().getProtocolValue().empty());
  EXPECT_EQ("websocket", upstream_request_->headers().getUpgradeValue());
  EXPECT_EQ("upgrade", upstream_request_->headers().getConnectionValue());

  Http::TestResponseHeaderMapImpl h1_upgrade_response{
      {":status", "101"}, {"upgrade", "websocket"}, {"connection", "upgrade"}};

  // Send response headers
  // The HTTP/1 upstream will observe the upgrade headers and needs to respond with 101
  // HTTP/2 and HTTP/3 upstreams need to respond to extended CONNECT with 200
  upstream_request_->encodeHeaders(upstreamProtocol() == Http::CodecType::HTTP1
                                       ? h1_upgrade_response
                                       : default_response_headers_,
                                   false);

  // Wait for them to arrive downstream.
  response_->waitForHeaders();
  // The test client codec will transform 200 extended CONNECT response to 101
  EXPECT_EQ("101", response_->headers().getStatusValue());

  // Make sure that even once the response has started, that data can continue to go upstream.
  codec_client_->sendData(*request_encoder_, "hello", false);
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Also test upstream to downstream data.
  upstream_request_->encodeData(12, false);
  response_->waitForBodyData(12);

  cleanupUpstreamAndDownstream();
}

TEST_P(ProxyingConnectIntegrationTest, ProxyConnectWithPortStripping) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) {
        hcm.set_strip_any_host_port(true);
        auto* route_config = hcm.mutable_route_config();
        auto* header_value_option = route_config->mutable_request_headers_to_add()->Add();
        auto* mutable_header = header_value_option->mutable_header();
        mutable_header->set_key("Host-In-Envoy");
        mutable_header->set_value("%REQ(:AUTHORITY)%");
      });

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
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "CONNECT");
  EXPECT_EQ(upstream_request_->headers().getHostValue(), "foo.lyft.com:80");
  auto stripped_host = upstream_request_->headers().get(Http::LowerCaseString("host-in-envoy"));
  ASSERT_EQ(stripped_host.size(), 1);
  EXPECT_EQ(stripped_host[0]->value(), "foo.lyft.com");

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

  // Send response headers
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Wait for them to arrive downstream.
  response_->waitForHeaders();
  EXPECT_EQ("200", response_->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

TEST_P(ProxyingConnectIntegrationTest, 2xxStatusCode) {
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

  // Send valid response headers, in HTTP1 all status codes in the 2xx range
  // are considered valid.
  default_response_headers_.setStatus(enumToInt(Http::Code::Accepted));

  // Send response headers
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Wait for them to arrive downstream.
  response_->waitForHeaders();
  EXPECT_EQ("202", response_->headers().getStatusValue());

  cleanupUpstreamAndDownstream();
}

using HttpFilterProto =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;
// Tunneling downstream TCP over an upstream HTTP CONNECT tunnel.
class TcpTunnelingIntegrationTest : public BaseTcpTunnelingIntegrationTest {
public:
  void SetUp() override {
    enableHalfClose(true);

    config_helper_.addConfigModifier(
        [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
          proxy_config.set_stat_prefix("tcp_stats");
          proxy_config.set_cluster("cluster_0");
          proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");

          auto* listener = bootstrap.mutable_static_resources()->add_listeners();
          listener->set_name("tcp_proxy");

          if (downstream_buffer_limit_ != 0) {
            listener->mutable_per_connection_buffer_limit_bytes()->set_value(
                downstream_buffer_limit_);
          }
          auto* socket_address = listener->mutable_address()->mutable_socket_address();
          socket_address->set_address(Network::Test::getLoopbackAddressString(version_));
          socket_address->set_port_value(0);

          auto* filter_chain = listener->add_filter_chains();
          auto* filter = filter_chain->add_filters();
          filter->mutable_typed_config()->PackFrom(proxy_config);
          filter->set_name("envoy.filters.network.tcp_proxy");
        });
    BaseTcpTunnelingIntegrationTest::SetUp();
  }

  void addHttpUpstreamFilterToCluster(const HttpFilterProto& config) {
    config_helper_.addConfigModifier([config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      ConfigHelper::HttpProtocolOptions protocol_options =
          MessageUtil::anyConvert<ConfigHelper::HttpProtocolOptions>(
              (*cluster->mutable_typed_extension_protocol_options())
                  ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]);
      *protocol_options.add_http_filters() = config;
      (*cluster->mutable_typed_extension_protocol_options())
          ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
              .PackFrom(protocol_options);
    });
  }

  const HttpFilterProto getAddHeaderFilterConfig(const std::string& name, const std::string& key,
                                                 const std::string& value) {
    HttpFilterProto filter_config;
    filter_config.set_name(name);
    auto configuration = test::integration::filters::AddHeaderFilterConfig();
    configuration.set_header_key(key);
    configuration.set_header_value(value);
    filter_config.mutable_typed_config()->PackFrom(configuration);
    return filter_config;
  }

  const HttpFilterProto getStopAndContinueFilterConfig() {
    HttpFilterProto filter_config;
    filter_config.set_name("stop-iteration-and-continue-filter");
    auto configuration = test::integration::filters::StopAndContinueConfig();
    configuration.set_stop_and_buffer(true);
    filter_config.mutable_typed_config()->PackFrom(configuration);
    return filter_config;
  }

  const HttpFilterProto getCodecFilterConfig() {
    HttpFilterProto filter_config;
    filter_config.set_name("envoy.filters.http.upstream_codec");
    auto configuration = envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec();
    filter_config.mutable_typed_config()->PackFrom(configuration);
    return filter_config;
  }

  void setUpConnection(FakeHttpConnectionPtr& fake_upstream_connection) {
    // Start a connection, and verify the upgrade headers are received upstream.
    tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
    if (!fake_upstream_connection) {
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection));
    }
    ASSERT_TRUE(fake_upstream_connection->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

    // Send upgrade headers downstream, fully establishing the connection.
    upstream_request_->encodeHeaders(default_response_headers_, false);
  }

  void sendBidiData(FakeHttpConnectionPtr& fake_upstream_connection, bool send_goaway = false) {
    // Send some data from downstream to upstream, and make sure it goes through.
    ASSERT_TRUE(tcp_client_->write("hello", false));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

    if (send_goaway) {
      fake_upstream_connection->encodeGoAway();
    }
    // Send data from upstream to downstream.
    upstream_request_->encodeData(12, false);
    ASSERT_TRUE(tcp_client_->waitForData(12));
  }

  void closeConnection(FakeHttpConnectionPtr& fake_upstream_connection) {
    // Now send more data and close the TCP client. This should be treated as half close, so the
    // data should go through.
    ASSERT_TRUE(tcp_client_->write("hello", false));
    tcp_client_->close();
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
    if (upstreamProtocol() == Http::CodecType::HTTP1) {
      ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
    } else {
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
      // If the upstream now sends 'end stream' the connection is fully closed.
      upstream_request_->encodeData(0, true);
    }
  }

  void testGiantRequestAndResponse(uint64_t request_size, uint64_t response_size) {
    // Start a connection, and verify the upgrade headers are received upstream.
    tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

    // Send response headers downstream, fully establishing the connection.
    upstream_request_->encodeHeaders(default_response_headers_, false);

    ASSERT_TRUE(tcp_client_->write(std::string(request_size, 'a'), false));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, request_size));

    upstream_request_->encodeData(response_size, false);
    ASSERT_TRUE(tcp_client_->waitForData(response_size));
    //  Finally close and clean up.

    tcp_client_->close();
    if (upstreamProtocol() == Http::CodecType::HTTP1) {
      ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    } else {
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    }
  }
  int downstream_buffer_limit_{0};
  IntegrationTcpClientPtr tcp_client_;
};

TEST_P(TcpTunnelingIntegrationTest, Basic) {
  initialize();

  setUpConnection(fake_upstream_connection_);
  sendBidiData(fake_upstream_connection_);
  closeConnection(fake_upstream_connection_);
}

TEST_P(TcpTunnelingIntegrationTest, UpstreamHttpFilters) {
  if (!(GetParam().tunneling_with_upstream_filters)) {
    return;
  }
  addHttpUpstreamFilterToCluster(getAddHeaderFilterConfig("add_header", "foo", "bar"));
  addHttpUpstreamFilterToCluster(getCodecFilterConfig());
  initialize();

  setUpConnection(fake_upstream_connection_);
  sendBidiData(fake_upstream_connection_);
  EXPECT_EQ(
      "bar",
      upstream_request_->headers().get(Http::LowerCaseString("foo"))[0]->value().getStringView());
  closeConnection(fake_upstream_connection_);
}

TEST_P(TcpTunnelingIntegrationTest, UpstreamHttpFiltersPauseAndResume) {
  if (!(GetParam().tunneling_with_upstream_filters)) {
    return;
  }
  addHttpUpstreamFilterToCluster(getStopAndContinueFilterConfig());
  addHttpUpstreamFilterToCluster(getCodecFilterConfig());
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send upgrade headers downstream, fully establishing the connection.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  bool verify_no_remote_close = true;
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    // in HTTP1 case, the connection is closed on stream reset and therefore, it
    // is possible to detect a remote close if remote FIN event gets processed before local close
    // socket event. By sending verify_no_remote_close as false to the write function, we are
    // allowing the test to pass even if remote close is detected.
    verify_no_remote_close = false;
  }
  // send some data to pause the filter
  ASSERT_TRUE(tcp_client_->write("hello", false, verify_no_remote_close));
  // send end stream to resume the filter
  ASSERT_TRUE(tcp_client_->write("hello", true, verify_no_remote_close));

  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 10));

  // Finally close and clean up.
  tcp_client_->close();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  }
}

TEST_P(TcpTunnelingIntegrationTest, FlowControlOnAndGiantBody) {
  downstream_buffer_limit_ = 1024;
  config_helper_.setBufferLimits(1024, 2024);
  initialize();

  testGiantRequestAndResponse(10 * 1024 * 1024, 10 * 1024 * 1024);
}

TEST_P(TcpTunnelingIntegrationTest, SendDataUpstreamAfterUpstreamClose) {
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    // HTTP/1.1 can't frame with FIN bits.
    return;
  }
  initialize();

  setUpConnection(fake_upstream_connection_);
  sendBidiData(fake_upstream_connection_);
  // Close upstream.
  upstream_request_->encodeData(2, true);
  tcp_client_->waitForHalfClose();

  // Now send data upstream.
  ASSERT_TRUE(tcp_client_->write("hello", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  // Finally close and clean up.
  tcp_client_->close();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  }
}

TEST_P(TcpTunnelingIntegrationTest, SendDataUpstreamAfterUpstreamCloseConnection) {
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    // HTTP/1.1 can't frame with FIN bits.
    return;
  }
  initialize();

  setUpConnection(fake_upstream_connection_);
  sendBidiData(fake_upstream_connection_);

  // Close upstream request and imitate idle connection closure.
  upstream_request_->encodeData(2, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  tcp_client_->waitForHalfClose();

  // Now send data upstream.
  ASSERT_TRUE(tcp_client_->write("hello", false));

  // Finally close and clean up.
  tcp_client_->close();
}

TEST_P(TcpTunnelingIntegrationTest, BasicUsePost) {
  // Enable using POST.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");
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
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().Method)[0]->value(), "POST");

  // Send upgrade headers downstream, fully establishing the connection.
  upstream_request_->encodeHeaders(default_response_headers_, false);

  sendBidiData(fake_upstream_connection_);
  closeConnection(fake_upstream_connection_);
}

TEST_P(TcpTunnelingIntegrationTest, TcpTunnelingAccessLog) {
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    return;
  }

  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("host.com:80");

    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "%ACCESS_LOG_TYPE%-%UPSTREAM_CONNECTION_ID%\n");
    access_log_config.set_path(access_log_filename);
    proxy_config.add_access_log()->mutable_typed_config()->PackFrom(access_log_config);

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
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "CONNECT");

  upstream_request_->encodeHeaders(default_response_headers_, false);
  sendBidiData(fake_upstream_connection_);
  closeConnection(fake_upstream_connection_);

  auto log_result = waitForAccessLog(access_log_filename);
  std::vector<std::string> access_log_parts = absl::StrSplit(log_result, '-');
  EXPECT_EQ(AccessLogType_Name(AccessLog::AccessLogType::TcpConnectionEnd), access_log_parts[0]);
  uint32_t upstream_connection_id;
  ASSERT_TRUE(absl::SimpleAtoi(access_log_parts[1], &upstream_connection_id));
  EXPECT_GT(upstream_connection_id, 0);
}

TEST_P(TcpTunnelingIntegrationTest, BasicHeaderEvaluationTunnelingConfig) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());

  // Set the "downstream-local-ip" header in the CONNECT request.
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");
    auto new_header = proxy_config.mutable_tunneling_config()->mutable_headers_to_add()->Add();
    new_header->mutable_header()->set_key("downstream-local-ip");
    new_header->mutable_header()->set_value("%DOWNSTREAM_LOCAL_ADDRESS_WITHOUT_PORT%");

    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "RESPONSE_HEADERS=%FILTER_STATE(envoy.tcp_proxy.propagate_response_headers:TYPED)% "
        "RESPONSE_TRAILERS=%FILTER_STATE(envoy.tcp_proxy.propagate_response_trailers:TYPED)%\n");
    access_log_config.set_path(access_log_filename);
    proxy_config.add_access_log()->mutable_typed_config()->PackFrom(access_log_config);

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
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "CONNECT");

  // Verify that the connect request has a "downstream-local-ip" header and its value is the
  // loopback address.
  EXPECT_EQ(
      upstream_request_->headers().get(Envoy::Http::LowerCaseString("downstream-local-ip")).size(),
      1);
  EXPECT_EQ(upstream_request_->headers()
                .get(Envoy::Http::LowerCaseString("downstream-local-ip"))[0]
                ->value()
                .getStringView(),
            Network::Test::getLoopbackAddressString(version_));

  // Send upgrade headers downstream, fully establishing the connection.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  sendBidiData(fake_upstream_connection_);
  closeConnection(fake_upstream_connection_);

  // Verify response header value object is not present
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr("RESPONSE_HEADERS=-"));
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr("RESPONSE_TRAILERS=-"));
}

// Verify that the header evaluator is updated without lifetime issue.
TEST_P(TcpTunnelingIntegrationTest, HeaderEvaluatorConfigUpdate) {
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");
    auto address_header = proxy_config.mutable_tunneling_config()->mutable_headers_to_add()->Add();
    address_header->mutable_header()->set_key("config-version");
    address_header->mutable_header()->set_value("1");

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
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  EXPECT_EQ(upstream_request_->headers().getMethodValue(), "CONNECT");

  EXPECT_EQ(upstream_request_->headers()
                .get(Envoy::Http::LowerCaseString("config-version"))[0]
                ->value()
                .getStringView(),
            "1");

  // Send upgrade headers downstream, fully establishing the connection.
  upstream_request_->encodeHeaders(default_response_headers_, false);
  ASSERT_TRUE(tcp_client_->write("hello", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
  new_config_helper.addConfigModifier(
      [&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
        auto* header =
            proxy_config.mutable_tunneling_config()->mutable_headers_to_add()->Mutable(0);
        header->mutable_header()->set_value("2");

        auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();
        for (auto& listener : *listeners) {
          if (listener.name() != "tcp_proxy") {
            continue;
          }
          // trigger full listener update.
          (*(*listener.mutable_metadata()->mutable_filter_metadata())["random_filter_name"]
                .mutable_fields())["random_key"]
              .set_number_value(2);
          auto* filter_chain = listener.mutable_filter_chains(0);
          auto* filter = filter_chain->mutable_filters(0);
          filter->mutable_typed_config()->PackFrom(proxy_config);
          break;
        }
      });
  new_config_helper.setLds("1");

  test_server_->waitForCounterEq("listener_manager.listener_modified", 1);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_warming", 0);
  test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", 0);

  // Start a connection, and verify the upgrade headers are received upstream.
  auto tcp_client_2 = makeTcpConnection(lookupPort("tcp_proxy"));

  // The upstream http2 or http3 connection is reused.
  ASSERT_TRUE(fake_upstream_connection_ != nullptr);

  FakeStreamPtr upstream_request_2;
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_2));
  ASSERT_TRUE(upstream_request_2->waitForHeadersComplete());
  // Verify the tcp proxy new header evaluator is applied.
  EXPECT_EQ(upstream_request_2->headers()
                .get(Envoy::Http::LowerCaseString("config-version"))[0]
                ->value()
                .getStringView(),
            "2");
  upstream_request_2->encodeHeaders(default_response_headers_, false);

  tcp_client_->close();
  tcp_client_2->close();

  ASSERT_TRUE(upstream_request_2->waitForEndStream(*dispatcher_));
  // If the upstream now sends 'end stream' the connection is fully closed.
  upstream_request_2->encodeData(0, true);
  ASSERT_TRUE(fake_upstream_connection_->waitForNoPost());
}

TEST_P(TcpTunnelingIntegrationTest, Goaway) {
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  initialize();

  // Send bidirectional data, including a goaway.
  // This should result in the first connection being torn down.
  setUpConnection(fake_upstream_connection_);
  sendBidiData(fake_upstream_connection_, true);
  closeConnection(fake_upstream_connection_);
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy", 1);

  // Make sure a subsequent connection can be established successfully.
  FakeHttpConnectionPtr fake_upstream_connection;
  setUpConnection(fake_upstream_connection);
  sendBidiData(fake_upstream_connection);
  closeConnection(fake_upstream_connection_);

  // Make sure the last stream is finished before doing test teardown.
  fake_upstream_connection->encodeGoAway();
  test_server_->waitForCounterGe("cluster.cluster_0.upstream_cx_destroy", 2);
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
}

TEST_P(TcpTunnelingIntegrationTest, InvalidResponseHeaders) {
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send invalid response_ headers, and verify that the client disconnects and
  // upstream gets a stream reset.
  default_response_headers_.setStatus(enumToInt(Http::Code::ServiceUnavailable));
  upstream_request_->encodeHeaders(default_response_headers_, false);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  // The connection should be fully closed, but the client has no way of knowing
  // that. Ensure the FIN is read and clean up state.
  tcp_client_->waitForHalfClose();
  tcp_client_->close();
}

TEST_P(TcpTunnelingIntegrationTest, CopyValidResponseHeaders) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");
    proxy_config.mutable_tunneling_config()->set_propagate_response_headers(true);

    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "%FILTER_STATE(envoy.tcp_proxy.propagate_response_headers:TYPED)%\n");
    access_log_config.set_path(access_log_filename);
    proxy_config.add_access_log()->mutable_typed_config()->PackFrom(access_log_config);

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
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send upgrade headers downstream, fully establishing the connection.
  const std::string header_value = "secret-value";
  default_response_headers_.addCopy("test-header-name", header_value);
  upstream_request_->encodeHeaders(default_response_headers_, false);

  sendBidiData(fake_upstream_connection_);
  closeConnection(fake_upstream_connection_);

  // Verify response header value is in the access log.
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr(header_value));
}

TEST_P(TcpTunnelingIntegrationTest, CopyInvalidResponseHeaders) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");
    proxy_config.mutable_tunneling_config()->set_propagate_response_headers(true);

    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "%FILTER_STATE(envoy.tcp_proxy.propagate_response_headers:TYPED)%\n");
    access_log_config.set_path(access_log_filename);
    proxy_config.add_access_log()->mutable_typed_config()->PackFrom(access_log_config);

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
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send invalid response headers.
  default_response_headers_.setStatus(enumToInt(Http::Code::ServiceUnavailable));
  const std::string header_value = "secret-value";
  default_response_headers_.addCopy("test-header-name", header_value);
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // verify that the client disconnects and upstream gets a stream reset.
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  // The connection should be fully closed, but the client has no way of knowing
  // that. Ensure the FIN is read and clean up state.
  tcp_client_->waitForHalfClose();
  tcp_client_->close();

  // Verify response header value is in the access log.
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr(header_value));
}

TEST_P(TcpTunnelingIntegrationTest, CopyInvalidResponseHeadersWithRetry) {
  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");
    proxy_config.mutable_tunneling_config()->set_propagate_response_headers(true);
    proxy_config.mutable_max_connect_attempts()->set_value(2);

    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "%FILTER_STATE(envoy.tcp_proxy.propagate_response_headers:TYPED)%\n");
    access_log_config.set_path(access_log_filename);
    proxy_config.add_access_log()->mutable_typed_config()->PackFrom(access_log_config);

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
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send invalid response headers.
  default_response_headers_.setStatus(enumToInt(Http::Code::ServiceUnavailable));
  const std::string header_value = "secret-value";
  default_response_headers_.addCopy("test-header-name", header_value);
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // The TCP proxy will create another request since the first failed and a retry is configured.
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  }

  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // The connection should be fully closed, but the client has no way of knowing
  // that. Ensure the FIN is read and clean up state.
  tcp_client_->waitForHalfClose();
  tcp_client_->close();

  // Verify response header value is in the access log.
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr(header_value));
}

TEST_P(TcpTunnelingIntegrationTest, CopyResponseTrailers) {
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }

  const std::string access_log_filename =
      TestEnvironment::temporaryPath(TestUtility::uniqueFilename());
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");
    proxy_config.mutable_tunneling_config()->set_propagate_response_trailers(true);

    envoy::extensions::access_loggers::file::v3::FileAccessLog access_log_config;
    access_log_config.mutable_log_format()->mutable_text_format_source()->set_inline_string(
        "%FILTER_STATE(envoy.tcp_proxy.propagate_response_trailers:TYPED)%\n");
    access_log_config.set_path(access_log_filename);
    proxy_config.add_access_log()->mutable_typed_config()->PackFrom(access_log_config);

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
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  upstream_request_->encodeHeaders(default_response_headers_, false);
  sendBidiData(fake_upstream_connection_);

  // Send trailers
  const std::string trailer_value = "trailer-value";
  Http::TestResponseTrailerMapImpl response_trailers{{"test-trailer-name", trailer_value}};
  upstream_request_->encodeTrailers(response_trailers);

  // Close Connection
  ASSERT_TRUE(tcp_client_->write("hello", false));
  tcp_client_->close();
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Verify response trailer value is in the access log.
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr(trailer_value));
}

TEST_P(TcpTunnelingIntegrationTest, DownstreamFinOnUpstreamTrailers) {
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }

  initialize();

  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  upstream_request_->encodeHeaders(default_response_headers_, false);
  sendBidiData(fake_upstream_connection_);

  // Send trailers
  const std::string trailer_value = "trailer-value";
  Http::TestResponseTrailerMapImpl response_trailers{{"test-trailer-name", trailer_value}};
  upstream_request_->encodeTrailers(response_trailers);

  // Upstream trailers should close the downstream connection for writing.
  tcp_client_->waitForHalfClose();

  // Close Connection
  tcp_client_->close();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
}

TEST_P(TcpTunnelingIntegrationTest, CloseUpstreamFirst) {
  initialize();

  setUpConnection(fake_upstream_connection_);
  sendBidiData(fake_upstream_connection_);

  // Send data from upstream to downstream with an end stream and make sure the data is received
  // before the connection is half-closed.
  upstream_request_->encodeData(12, true);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->close());
  }
  ASSERT_TRUE(tcp_client_->waitForData(12));
  tcp_client_->waitForHalfClose();

  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    tcp_client_->close();
  } else {
    // Attempt to send data upstream.
    // should go through.
    ASSERT_TRUE(tcp_client_->write("hello", false));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

    ASSERT_TRUE(tcp_client_->write("hello", true));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  }
}

TEST_P(TcpTunnelingIntegrationTest, ResetStreamTest) {
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  enableHalfClose(false);
  initialize();

  setUpConnection(fake_upstream_connection_);

  // Reset the stream.
  upstream_request_->encodeResetStream();
  tcp_client_->waitForDisconnect();
}

TEST_P(TcpTunnelingIntegrationTest, UpstreamConnectingDownstreamDisconnect) {
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }

#if defined(WIN32)
  // TODO(ggreenway): figure out why this test fails on Windows and remove this disable.
  // Failing tests:
  // IpAndHttpVersions/TcpTunnelingIntegrationTest.UpstreamConnectingDownstreamDisconnect/IPv4_HttpDownstream_Http3UpstreamBareHttp2,
  // IpAndHttpVersions/TcpTunnelingIntegrationTest.UpstreamConnectingDownstreamDisconnect/IPv6_HttpDownstream_Http2UpstreamWrappedHttp2,
  // Times out at the end of the test on `ASSERT_TRUE(upstream_request_->waitForReset());`.
  return;
#endif

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy proxy_config;
    proxy_config.set_stat_prefix("tcp_stats");
    proxy_config.set_cluster("cluster_0");
    proxy_config.mutable_tunneling_config()->set_hostname("foo.lyft.com:80");

    // Enable retries. The crash is due to retrying after the downstream connection is closed,
    // which can't occur if retries are not enabled.
    proxy_config.mutable_max_connect_attempts()->set_value(2);

    auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();
    for (auto& listener : *listeners) {
      if (listener.name() != "tcp_proxy") {
        continue;
      }
      auto* filter_chain = listener.mutable_filter_chains(0);
      auto* filter = filter_chain->mutable_filters(0);
      filter->mutable_typed_config()->PackFrom(proxy_config);

      // Use TLS because it will respond to a TCP half-close during handshake by closing the
      // connection.
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
      ConfigHelper::initializeTls({}, *tls_context.mutable_common_tls_context(), false);
      filter_chain->mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
      filter_chain->mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);

      break;
    }
  });

  enableHalfClose(false);
  initialize();

  IntegrationTcpClientPtr tcp_client = makeTcpConnection(lookupPort("tcp_proxy"));

  // Wait for the request for a connection, but don't send a response back yet. This ensures that
  // tcp_proxy is stuck in `connecting_`.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Close the client connection. The TLS transport socket will detect this even while
  // `readDisable(true)` on the connection, and will raise a `RemoteClose` event.
  tcp_client->close();

  ASSERT_TRUE(upstream_request_->waitForReset());
  ASSERT_TRUE(fake_upstream_connection_->close());
}

TEST_P(TcpTunnelingIntegrationTest, TestIdletimeoutWithLargeOutstandingData) {
  enableHalfClose(false);
  config_helper_.setBufferLimits(1024, 1024);
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(1);
    auto* filter_chain = listener->mutable_filter_chains(0);
    auto* config_blob = filter_chain->mutable_filters(0)->mutable_typed_config();

    ASSERT_TRUE(config_blob->Is<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>());
    auto tcp_proxy_config =
        MessageUtil::anyConvert<envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy>(
            *config_blob);
    tcp_proxy_config.mutable_idle_timeout()->set_nanos(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(500))
            .count());
    config_blob->PackFrom(tcp_proxy_config);
  });

  initialize();

  setUpConnection(fake_upstream_connection_);

  std::string data(1024 * 16, 'a');
  ASSERT_TRUE(tcp_client_->write(data));
  upstream_request_->encodeData(data, false);

  tcp_client_->waitForDisconnect();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
    tcp_client_->close();
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

  setUpConnection(fake_upstream_connection_);

  tcp_client_->readDisable(true);
  std::string data(size, 'a');
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(tcp_client_->write("hello", false));
    ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

    upstream_request_->encodeData(data, true);
    ASSERT_TRUE(fake_upstream_connection_->close());
  } else {
    ASSERT_TRUE(tcp_client_->write("", true));

    // This ensures that readDisable(true) has been run on its thread
    // before tcp_client_ starts writing.
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    upstream_request_->encodeData(data, true);
  }

  test_server_->waitForCounterGe("cluster.cluster_0.upstream_flow_control_paused_reading_total", 1);
  tcp_client_->readDisable(false);
  tcp_client_->waitForData(data);
  tcp_client_->waitForHalfClose();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    tcp_client_->close();
  }
}

// Test that an upstream flush works correctly (all data is flushed)
TEST_P(TcpTunnelingIntegrationTest, TcpProxyUpstreamFlush) {
  if (upstreamProtocol() == Http::CodecType::HTTP3) {
    // The payload data depends on having TCP buffers upstream and downstream.
    // For HTTP/3, upstream, the flow control window will back up sooner, Envoy
    // flow control will kick in, and the large write of |data| will fail to
    // complete.
    return;
  }
  // Use a very large size to make sure it is larger than the kernel socket read buffer.
  const uint32_t size = 50 * 1024 * 1024;
  config_helper_.setBufferLimits(size, size);
  initialize();

  setUpConnection(fake_upstream_connection_);

  upstream_request_->readDisable(true);
  upstream_request_->encodeData("hello", false);

  // This ensures that fake_upstream_connection->readDisable has been run on its thread
  // before tcp_client_ starts writing.
  ASSERT_TRUE(tcp_client_->waitForData(5));

  std::string data(size, 'a');
  ASSERT_TRUE(tcp_client_->write(data, true));
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    tcp_client_->close();

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
    tcp_client_->waitForHalfClose();
  }
}

// Test that h2/h3 connection is reused.
TEST_P(TcpTunnelingIntegrationTest, ConnectionReuse) {
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    return;
  }
  initialize();

  setUpConnection(fake_upstream_connection_);

  // Send data in both directions.
  ASSERT_TRUE(tcp_client_->write("hello1", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello1"));

  // Send data from upstream to downstream with an end stream and make sure the data is received
  // before the connection is half-closed.
  upstream_request_->encodeData("world1", true);
  tcp_client_->waitForData("world1");
  tcp_client_->waitForHalfClose();
  tcp_client_->close();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Establish a new connection.
  IntegrationTcpClientPtr tcp_client_2 = makeTcpConnection(lookupPort("tcp_proxy"));

  // The new CONNECT stream is established in the existing h2 connection.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  ASSERT_TRUE(tcp_client_2->write("hello2", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello2"));

  // Send data from upstream to downstream with an end stream and make sure the data is received
  // before the connection is half-closed.
  upstream_request_->encodeData("world2", true);
  tcp_client_2->waitForData("world2");
  tcp_client_2->waitForHalfClose();
  tcp_client_2->close();
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
}

// Test that with HTTP1 we have no connection reuse with downstream close.
TEST_P(TcpTunnelingIntegrationTest, H1NoConnectionReuse) {
  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }
  initialize();

  setUpConnection(fake_upstream_connection_);

  // Send data in both directions.
  ASSERT_TRUE(tcp_client_->write("hello1", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello1"));

  // Send data from upstream to downstream and close the connection
  // from downstream.
  upstream_request_->encodeData("world1", false);
  tcp_client_->waitForData("world1");
  tcp_client_->close();

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Establish a new connection.
  IntegrationTcpClientPtr tcp_client_2 = makeTcpConnection(lookupPort("tcp_proxy"));
  // A new connection is established
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  ASSERT_TRUE(tcp_client_2->write("hello1", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello1"));
  tcp_client_2->close();

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

// Test that with HTTP1 we have no connection with upstream close.
TEST_P(TcpTunnelingIntegrationTest, H1UpstreamCloseNoConnectionReuse) {
  if (upstreamProtocol() == Http::CodecType::HTTP2) {
    return;
  }
  initialize();

  // Establish a connection.
  IntegrationTcpClientPtr tcp_client_1 = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Send data in both directions.
  ASSERT_TRUE(tcp_client_1->write("hello1", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello1"));

  // Send data from upstream to downstream and close the connection
  // from the upstream.
  upstream_request_->encodeData("world1", false);
  tcp_client_1->waitForData("world1");
  ASSERT_TRUE(fake_upstream_connection_->close());

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  tcp_client_1->waitForHalfClose();
  tcp_client_1->close();

  // Establish a new connection.
  IntegrationTcpClientPtr tcp_client_2 = makeTcpConnection(lookupPort("tcp_proxy"));
  // A new connection is established
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, false);

  ASSERT_TRUE(tcp_client_2->write("hello2", false));
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, "hello2"));
  ASSERT_TRUE(fake_upstream_connection_->close());

  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  tcp_client_2->waitForHalfClose();
  tcp_client_2->close();
}

TEST_P(TcpTunnelingIntegrationTest, 2xxStatusCodeValidHttp1) {
  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Send valid response headers, in HTTP1 all status codes in the 2xx range
  // are considered valid.
  default_response_headers_.setStatus(enumToInt(Http::Code::Accepted));
  upstream_request_->encodeHeaders(default_response_headers_, false);

  sendBidiData(fake_upstream_connection_);

  // Close the downstream connection and wait for upstream disconnect
  tcp_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(TcpTunnelingIntegrationTest, ContentLengthHeaderIgnoredHttp1) {
  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
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
  ASSERT_TRUE(tcp_client_->waitForData(12));

  // Now send some data and close the TCP client.
  ASSERT_TRUE(tcp_client_->write("hello", false));
  tcp_client_->close();
  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(TcpTunnelingIntegrationTest, TransferEncodingHeaderIgnoredHttp1) {
  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));
  // Using raw connection to be able to set Transfer-encoding header.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  std::string data;
  ASSERT_TRUE(fake_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &data));
  ASSERT_THAT(data, testing::HasSubstr("CONNECT foo.lyft.com:80 HTTP/1.1"));

  // Send upgrade headers downstream, fully establishing the connection.
  ASSERT_TRUE(
      fake_upstream_connection->write("HTTP/1.1 200 OK\r\nTransfer-encoding: chunked\r\n\r\n"));

  // Now send some data and close the TCP client.
  ASSERT_TRUE(tcp_client_->write("hello"));
  ASSERT_TRUE(
      fake_upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("hello")));

  // Close connections.
  ASSERT_TRUE(fake_upstream_connection->close());
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());
  tcp_client_->close();
}

TEST_P(TcpTunnelingIntegrationTest, DeferTransmitDataUntilSuccessConnectResponseIsReceived) {
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send some data straight away.
  ASSERT_TRUE(tcp_client_->write("hello", false));

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Wait a bit, no data should go through.
  ASSERT_FALSE(upstream_request_->waitForData(*dispatcher_, 1, std::chrono::milliseconds(100)));

  upstream_request_->encodeHeaders(default_response_headers_, false);

  ASSERT_TRUE(upstream_request_->waitForData(*dispatcher_, 5));

  tcp_client_->close();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    // If the upstream now sends 'end stream' the connection is fully closed.
    upstream_request_->encodeData(0, true);
  }
}

TEST_P(TcpTunnelingIntegrationTest, NoDataTransmittedIfConnectFailureResponseIsReceived) {
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));

  // Send some data straight away.
  ASSERT_TRUE(tcp_client_->write("hello", false));

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  default_response_headers_.setStatus(enumToInt(Http::Code::ServiceUnavailable));
  upstream_request_->encodeHeaders(default_response_headers_, false);

  // Wait a bit, no data should go through.
  ASSERT_FALSE(upstream_request_->waitForData(*dispatcher_, 1, std::chrono::milliseconds(100)));

  tcp_client_->close();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  } else {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
}

TEST_P(TcpTunnelingIntegrationTest, UpstreamDisconnectBeforeResponseReceived) {
  initialize();

  // Start a connection, and verify the upgrade headers are received upstream.
  tcp_client_ = makeTcpConnection(lookupPort("tcp_proxy"));

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  ASSERT_TRUE(fake_upstream_connection_->close());
  tcp_client_->waitForHalfClose();
  tcp_client_->close();
}

INSTANTIATE_TEST_SUITE_P(
    IpAndHttpVersions, TcpTunnelingIntegrationTest,
    testing::ValuesIn(BaseTcpTunnelingIntegrationTest::getProtocolTestParams(
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3},
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3})),
    BaseTcpTunnelingIntegrationTest::protocolTestParamsToString);
} // namespace
} // namespace Envoy
