#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/upstreams/http/tcp/v3/tcp_connection_pool.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Terminating CONNECT and sending raw TCP upstream.
class ConnectTerminationIntegrationTest : public HttpProtocolIntegrationTest {
public:
  ConnectTerminationIntegrationTest() { enableHalfClose(true); }

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_delayed_close_timeout()->set_seconds(delay_close_seconds_);
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
                                                  {":path", "/"},
                                                  {":protocol", "bytestream"},
                                                  {":scheme", "https"},
                                                  {":authority", "foo.lyft.com:80"}};
  void clearExtendedConnectHeaders() {
    connect_headers_.removeProtocol();
    connect_headers_.removePath();
  }

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
  }

  FakeRawConnectionPtr fake_raw_upstream_connection_;
  IntegrationStreamDecoderPtr response_;
  uint32_t delay_close_seconds_ = 200;
  bool terminate_via_cluster_config_{};
  bool enable_timeout_{};
  bool exact_match_{};
  bool allow_post_{};
};

TEST_P(ConnectTerminationIntegrationTest, OriginalStyle) {
  initialize();
  clearExtendedConnectHeaders();

  setUpConnection();
  sendBidirectionalDataAndCleanShutdown();
}

TEST_P(ConnectTerminationIntegrationTest, Basic) {
  initialize();

  setUpConnection();
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
  delay_close_seconds_ = 1;
  allow_post_ = true;
  initialize();

  // Use POST request.
  connect_headers_.setMethod("POST");
  connect_headers_.removeProtocol();

  setUpConnection();
  sendBidirectionalDataAndCleanShutdown();
}

TEST_P(ConnectTerminationIntegrationTest, UsingHostMatch) {
  exact_match_ = true;
  initialize();

  connect_headers_.removePath();
  connect_headers_.removeProtocol();

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
  response_ = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "CONNECT"},
                                     {":path", "/"},
                                     {":protocol", "bytestream"},
                                     {":scheme", "https"},
                                     {":authority", "foo.lyft.com:80"}});
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

// For this class, forward the CONNECT request upstream
class ProxyingConnectIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          ConfigHelper::setConnectConfig(hcm, false, false,
                                         downstream_protocol_ == Http::CodecType::HTTP3);
        });

    HttpProtocolIntegrationTest::initialize();
  }

  Http::TestRequestHeaderMapImpl connect_headers_{{":method", "CONNECT"},
                                                  {":authority", "foo.lyft.com:80"}};
  IntegrationStreamDecoderPtr response_;
};

INSTANTIATE_TEST_SUITE_P(
    Protocols, ProxyingConnectIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3},
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3})),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(ProxyingConnectIntegrationTest, ProxyConnectLegacy) {
#ifdef ENVOY_ENABLE_UHV
  // TODO(#23286) - add web socket support for H2 UHV
  return;
#endif

  config_helper_.addRuntimeOverride("envoy.reloadable_features.use_rfc_connect", "false");

  initialize();

  Http::TestRequestHeaderMapImpl legacy_connect_headers{{":method", "CONNECT"},
                                                        {":path", "/"},
                                                        {":protocol", "bytestream"},
                                                        {":scheme", "https"},
                                                        {":authority", "foo.lyft.com:80"}};
  // Send request headers.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(legacy_connect_headers);
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
    EXPECT_EQ(upstream_request_->headers().getProtocolValue(), "bytestream");
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

INSTANTIATE_TEST_SUITE_P(HttpAndIpVersions, ConnectTerminationIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

using Params = std::tuple<Network::Address::IpVersion, Http::CodecType>;

// Tunneling downstream TCP over an upstream HTTP CONNECT tunnel.
class TcpTunnelingIntegrationTest : public HttpProtocolIntegrationTest {
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
          auto* socket_address = listener->mutable_address()->mutable_socket_address();
          socket_address->set_address(Network::Test::getLoopbackAddressString(version_));
          socket_address->set_port_value(0);

          auto* filter_chain = listener->add_filter_chains();
          auto* filter = filter_chain->add_filters();
          filter->mutable_typed_config()->PackFrom(proxy_config);
          filter->set_name("envoy.filters.network.tcp_proxy");
        });
    HttpProtocolIntegrationTest::SetUp();
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

  IntegrationTcpClientPtr tcp_client_;
};

TEST_P(TcpTunnelingIntegrationTest, Basic) {
  initialize();

  setUpConnection(fake_upstream_connection_);
  sendBidiData(fake_upstream_connection_);
  closeConnection(fake_upstream_connection_);
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
        "FILTER_STATE=%FILTER_STATE(envoy.tcp_proxy.propagate_response_headers:TYPED)%\n");
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
  EXPECT_THAT(waitForAccessLog(access_log_filename), testing::HasSubstr("FILTER_STATE=-"));
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

  ConfigHelper new_config_helper(
      version_, *api_, MessageUtil::getJsonStringFromMessageOrDie(config_helper_.bootstrap()));
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

TEST_P(TcpTunnelingIntegrationTest, CopyResponseHeaders) {
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

    // Enable retries. The crash is due to retrying after the downstream connection is closed, which
    // can't occur if retries are not enabled.
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
      ConfigHelper::initializeTls({}, *tls_context.mutable_common_tls_context());
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
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3},
        {Http::CodecType::HTTP1, Http::CodecType::HTTP2, Http::CodecType::HTTP3})),
    HttpProtocolIntegrationTest::protocolTestParamsToString);
} // namespace
} // namespace Envoy
