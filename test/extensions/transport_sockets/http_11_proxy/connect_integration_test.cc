#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/extensions/key_value/file_based/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/integration.h"

namespace Envoy {
namespace {

class Http11ConnectHttpIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public HttpIntegrationTest {
public:
  Http11ConnectHttpIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {
    upstream_tls_ = true;
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstream_connection_.reset();
    fake_upstreams_.clear();
  }

  void addDfpConfig() {
    const std::string filter =
        R"EOF(name: dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
  dns_cache_config:
    name: foo
)EOF";
    config_helper_.prependFilter(filter);

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      const std::string cluster_type_config =
          R"EOF(
name: envoy.clusters.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
  dns_cache_config:
    name: foo
)EOF";
      TestUtility::loadFromYaml(cluster_type_config, *cluster->mutable_cluster_type());
      cluster->clear_load_assignment();
      cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
    });
  }

  void initialize() override {
    TestEnvironment::writeStringToFileForTest("alt_svc_cache.txt", "");
    config_helper_.addFilter("{ name: header-to-proxy-filter }");
    if (try_http3_) {
      envoy::config::core::v3::AlternateProtocolsCacheOptions alt_cache;
      alt_cache.set_name("default_alternate_protocols_cache");
      const std::string filename = TestEnvironment::temporaryPath("alt_svc_cache.txt");
      envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig config;
      config.set_filename(filename);
      config.mutable_flush_interval()->set_nanos(0);
      envoy::config::common::key_value::v3::KeyValueStoreConfig kv_config;
      kv_config.mutable_config()->set_name("envoy.key_value.file_based");
      kv_config.mutable_config()->mutable_typed_config()->PackFrom(config);
      alt_cache.mutable_key_value_store_config()->set_name("envoy.common.key_value");
      alt_cache.mutable_key_value_store_config()->mutable_typed_config()->PackFrom(kv_config);

      config_helper_.configureUpstreamTls(use_alpn_, try_http3_, alt_cache);
    } else if (upstream_tls_) {
      config_helper_.configureUpstreamTls(use_alpn_, try_http3_);
    }
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      envoy::config::core::v3::TransportSocket inner_socket;
      inner_socket.CopyFrom(*transport_socket);
      if (inner_socket.name().empty()) {
        inner_socket.set_name("envoy.transport_sockets.raw_buffer");
      }
      transport_socket->set_name("envoy.transport_sockets.http_11_proxy");
      envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport
          transport;
      transport.mutable_transport_socket()->MergeFrom(inner_socket);
      transport_socket->mutable_typed_config()->PackFrom(transport);

      auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);

      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
      protocol_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
      if (upstream_tls_) {
        protocol_options.mutable_auto_config();
      } else {
        protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
      }
      ConfigHelper::setProtocolOptions(*cluster, protocol_options);
    });
    BaseIntegrationTest::initialize();
    if (upstream_tls_) {
      addFakeUpstream(createUpstreamTlsContext(upstreamConfig()), upstreamProtocol(), false);
      addFakeUpstream(createUpstreamTlsContext(upstreamConfig()), upstreamProtocol(), false);
      // Read disable the fake upstreams, so we can rawRead rather than read data and decrypt.
      fake_upstreams_[1]->setDisableAllAndDoNotEnable(true);
      fake_upstreams_[2]->setDisableAllAndDoNotEnable(true);
    } else {
      addFakeUpstream(upstreamProtocol());
      addFakeUpstream(upstreamProtocol());
    }

    if (try_http3_) {
      uint32_t port = fake_upstreams_[0]->localAddress()->ip()->port();
      std::string key = absl::StrCat("https://sni.lyft.com:", port);

      size_t seconds = std::chrono::duration_cast<std::chrono::seconds>(
                           timeSystem().monotonicTime().time_since_epoch())
                           .count();
      std::string value = absl::StrCat("h3=\":", port, "\"; ma=", 86400 + seconds, "|0|0");
      TestEnvironment::writeStringToFileForTest(
          "alt_svc_cache.txt", absl::StrCat(key.length(), "\n", key, value.length(), "\n", value));
    }
  }

  void stripConnectUpgradeAndRespond() {
    // Strip the CONNECT upgrade.
    std::string prefix_data;
    const std::string hostname(default_request_headers_.getHostValue());
    ASSERT_TRUE(fake_upstream_connection_->waitForInexactRawData("\r\n\r\n", &prefix_data));
    EXPECT_EQ(absl::StrCat("CONNECT ", hostname, ":443 HTTP/1.1\r\n\r\n"), prefix_data);

    // Ship the CONNECT response.
    fake_upstream_connection_->writeRawData("HTTP/1.1 200 OK\r\n\r\n");
  }
  bool use_alpn_ = false;
  bool try_http3_ = false;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, Http11ConnectHttpIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that with no connect-proxy header, the transport socket is a no-op.
TEST_P(Http11ConnectHttpIntegrationTest, NoHeader) {
  initialize();

  // With no connect-proxy header, the original request gets proxied to fake upstream 0.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("foo"), "bar");
  default_response_headers_.setCopy(Envoy::Http::LowerCaseString("foo"), "bar");
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("foo")).empty());
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("foo")).empty());

  // Second request reuses the connection.
  sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
}

// If sending to an HTTP upstream, no CONNECT header will be appended but a
// fully qualified URL will be sent.
TEST_P(Http11ConnectHttpIntegrationTest, CleartextRequestResponse) {
  upstream_tls_ = false;
  initialize();

  // Point at the second fake upstream. Envoy doesn't actually know about this one.
  absl::string_view second_upstream_address(fake_upstreams_[1]->localAddress()->asStringView());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // The connect-proxy header will be stripped by the header-to-proxy-filter and inserted as
  // metadata.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("connect-proxy"),
                                   second_upstream_address);
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 1, due to the connect-proxy header.
  FakeRawConnectionPtr fake_upstream_raw_connection_;
  ASSERT_TRUE(fake_upstreams_[1]->waitForRawConnection(fake_upstream_raw_connection_));
  std::string observed_data;
  ASSERT_TRUE(fake_upstream_raw_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("\r\n\r\n"), &observed_data));
  // There should be no CONNECT header.
  EXPECT_FALSE(absl::StrContains(observed_data, "CONNECT"));
  // The proxied request should use a fully qualified URL.
  EXPECT_TRUE(absl::StrContains(observed_data, "GET http://sni.lyft.com/test/long/url HTTP/1.1"))
      << observed_data;
  EXPECT_TRUE(absl::StrContains(observed_data, "host: sni.lyft.com"));

  // Send a response.
  auto response2 = "HTTP/1.1 200 OK\r\ncontent-length: 0\r\nbar: eep\r\n\r\n";
  ASSERT_TRUE(fake_upstream_raw_connection_->write(response2, false));

  // Wait for the response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("bar")).empty());
}
// Test sending 2 requests to one proxy
TEST_P(Http11ConnectHttpIntegrationTest, TestMultipleRequestsSignleEndpoint) {
  initialize();

  // Point at the second fake upstream. Envoy doesn't actually know about this one.
  absl::string_view second_upstream_address(fake_upstreams_[1]->localAddress()->asStringView());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // The connect-proxy header will be stripped by the header-to-proxy-filter and inserted as
  // metadata.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("connect-proxy"),
                                   second_upstream_address);
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 1, due to the connect-proxy header.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  stripConnectUpgradeAndRespond();

  // Enable reading on the new stream, and read the encapsulated request.
  ASSERT_TRUE(fake_upstream_connection_->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Send the encapsulated response.
  default_response_headers_.setCopy(Envoy::Http::LowerCaseString("bar"), "eep");
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Make sure the upgrade headers were swallowed and the second were received.
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("bar")).empty());

  // Now send a second request, and make sure it goes to the same upstream.
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 2, due to the connect-proxy header.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test sending requests to different proxies.
TEST_P(Http11ConnectHttpIntegrationTest, TestMultipleRequestsAndEndpoints) {
  initialize();

  // Point at the second fake upstream. Envoy doesn't actually know about this one.
  absl::string_view second_upstream_address(fake_upstreams_[1]->localAddress()->asStringView());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // The connect-proxy header will be stripped by the header-to-proxy-filter and inserted as
  // metadata.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("connect-proxy"),
                                   second_upstream_address);
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 1, due to the connect-proxy header.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));

  stripConnectUpgradeAndRespond();

  // Enable reading on the new stream, and read the encapsulated request.
  ASSERT_TRUE(fake_upstream_connection_->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Send the encapsulated response.
  default_response_headers_.setCopy(Envoy::Http::LowerCaseString("bar"), "eep");
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Make sure the upgrade headers were swallowed and the second were received.
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("bar")).empty());

  // Now send a second request, and make sure it goes to upstream 2.
  absl::string_view third_upstream_address(fake_upstreams_[2]->localAddress()->asStringView());
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("connect-proxy"),
                                   third_upstream_address);
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 2, due to the connect-proxy header.
  ASSERT_TRUE(fake_upstreams_[2]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  stripConnectUpgradeAndRespond();

  ASSERT_TRUE(fake_upstream_connection_->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test sending requests to different proxies.
TEST_P(Http11ConnectHttpIntegrationTest, TestMultipleRequestsSingleEndpoint) {
  // Also make sure that alpn negotiation works.
  use_alpn_ = true;
  initialize();

  // Point at the second fake upstream. Envoy doesn't actually know about this one.
  absl::string_view second_upstream_address(fake_upstreams_[1]->localAddress()->asStringView());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // The connect-proxy header will be stripped by the header-to-proxy-filter and inserted as
  // metadata.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("connect-proxy"),
                                   second_upstream_address);
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 1, due to the connect-proxy header.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  stripConnectUpgradeAndRespond();

  ASSERT_TRUE(fake_upstream_connection_->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Now send a second request to the same fake upstream. Envoy will pipeline and reuse the
  // connection so no need to strip the connect.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("request2"), "val2");
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  EXPECT_FALSE(upstream_request_->headers().get(Http::LowerCaseString("request2")).empty());

  upstream_request_->encodeHeaders(default_response_headers_, true);
  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Now send a request without the connect-proxy header. Make sure it doesn't get pooled in.
  default_request_headers_.remove(Envoy::Http::LowerCaseString("connect-proxy"));
  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 0.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  // No encapsulation.
  EXPECT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test Http2 for the inner application layer.
TEST_P(Http11ConnectHttpIntegrationTest, TestHttp2) {
  setUpstreamProtocol(Http::CodecType::HTTP2);
  use_alpn_ = true;
  initialize();

  // Point at the second fake upstream. Envoy doesn't actually know about this one.
  absl::string_view second_upstream_address(fake_upstreams_[1]->localAddress()->asStringView());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // The connect-proxy header will be stripped by the header-to-proxy-filter and inserted as
  // metadata.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("connect-proxy"),
                                   second_upstream_address);
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 1, due to the connect-proxy header.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  stripConnectUpgradeAndRespond();

  ASSERT_TRUE(fake_upstream_connection_->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

#ifdef ENVOY_ENABLE_QUIC

// Test Http3 failing to HTTP/2 if proxy settings are enabled.
TEST_P(Http11ConnectHttpIntegrationTest, TestHttp3Failover) {
  setUpstreamProtocol(Http::CodecType::HTTP2);
  use_alpn_ = true;
  try_http3_ = true;
  initialize();

  // Point at the second fake upstream. Envoy doesn't actually know about this one.
  absl::string_view second_upstream_address(fake_upstreams_[1]->localAddress()->asStringView());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // The connect-proxy header will be stripped by the header-to-proxy-filter and inserted as
  // metadata.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("connect-proxy"),
                                   second_upstream_address);
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 1, due to the connect-proxy header.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  stripConnectUpgradeAndRespond();

  ASSERT_TRUE(fake_upstream_connection_->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Test HTTP/3 being used if proxy settings are not set.
TEST_P(Http11ConnectHttpIntegrationTest, TestHttp3) {
  setUpstreamProtocol(Http::CodecType::HTTP3);
  use_alpn_ = true;
  try_http3_ = true;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 0.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

#endif

TEST_P(Http11ConnectHttpIntegrationTest, DfpNoProxyAddress) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  addDfpConfig();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Set the host to a domain which does not exist.
  default_request_headers_.setHost("doesnotexist.lyft.com");

  // Without the proxy header set, the filter will fail the request due to DNS resolution failure.
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), testing::HasSubstr("dns_resolution_failure"));
}

TEST_P(Http11ConnectHttpIntegrationTest, DfpWithProxyAddress) {
  addDfpConfig();

  initialize();

  // Point at the second fake upstream. Envoy doesn't actually know about this one.
  absl::string_view second_upstream_address(fake_upstreams_[1]->localAddress()->asStringView());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Set the host to a domain which does not exist.
  default_request_headers_.setHost("doesnotexist.lyft.com");
  // The connect-proxy header will be stripped by the header-to-proxy-filter and inserted as
  // metadata.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("connect-proxy"),
                                   second_upstream_address);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // The request should be sent to fake upstream 1, as DNS lookup is bypassed due to the
  // connect-proxy header.
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  stripConnectUpgradeAndRespond();

  ASSERT_TRUE(fake_upstream_connection_->readDisable(false));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(Http11ConnectHttpIntegrationTest, DfpWithProxyAddressLegacy) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.skip_dns_lookup_for_proxied_requests", "false");

  addDfpConfig();

  initialize();

  // Point at the second fake upstream. Envoy doesn't actually know about this one.
  absl::string_view second_upstream_address(fake_upstreams_[1]->localAddress()->asStringView());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  // Set the host to a domain which does not exist.
  default_request_headers_.setHost("doesnotexist.lyft.com");
  // The connect-proxy header will be stripped by the header-to-proxy-filter and inserted as
  // metadata.
  default_request_headers_.setCopy(Envoy::Http::LowerCaseString("connect-proxy"),
                                   second_upstream_address);

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  // Wait for the encapsulated response to be received.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

} // namespace
} // namespace Envoy
