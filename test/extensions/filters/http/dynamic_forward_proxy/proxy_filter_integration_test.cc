#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"

namespace Envoy {
namespace {

class ProxyFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  ProxyFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeWithArgs(uint64_t max_hosts = 1024, uint32_t max_pending_requests = 1024,
                          const std::string& override_auto_sni_header = "") {
    setUpstreamProtocol(Http::CodecType::HTTP1);
    const std::string filename = TestEnvironment::temporaryPath("dns_cache.txt");

    const std::string filter = fmt::format(R"EOF(
name: dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    dns_cache_circuit_breaker:
      max_pending_requests: {}
    key_value_config:
      config:
        name: envoy.key_value.file_based
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.key_value.file_based.v3.FileBasedKeyValueStoreConfig
          filename: {}
)EOF",
                                           Network::Test::ipVersionToDnsFamily(GetParam()),
                                           max_hosts, max_pending_requests, filename);
    config_helper_.prependFilter(filter);

    config_helper_.prependFilter(fmt::format(R"EOF(
name: stream-info-to-headers-filter
typed_config:
  "@type": type.googleapis.com/google.protobuf.Empty)EOF"));
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to CDS filesystem sourcing.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_path(cds_helper_.cds_path());
      bootstrap.mutable_static_resources()->clear_clusters();
    });

    // Set validate_clusters to false to allow us to reference a CDS cluster.
    config_helper_.addConfigModifier(
        [override_auto_sni_header](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_route_config()->mutable_validate_clusters()->set_value(false);
        });

    // Setup the initial CDS cluster.
    cluster_.mutable_connect_timeout()->CopyFrom(
        Protobuf::util::TimeUtil::MillisecondsToDuration(5000));
    cluster_.set_name("cluster_0");
    cluster_.set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);

    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
    if (!override_auto_sni_header.empty()) {
      protocol_options.mutable_upstream_http_protocol_options()->set_override_auto_sni_header(
          override_auto_sni_header);
    }
    protocol_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
    protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
    ConfigHelper::setProtocolOptions(cluster_, protocol_options);

    if (upstream_tls_) {
      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
      auto* validation_context =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
      cluster_.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
      cluster_.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
    }

    const std::string cluster_type_config = fmt::format(
        R"EOF(
name: envoy.clusters.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    dns_cache_circuit_breaker:
      max_pending_requests: {}
    key_value_config:
      config:
        name: envoy.key_value.file_based
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.key_value.file_based.v3.FileBasedKeyValueStoreConfig
          filename: {}
)EOF",
        Network::Test::ipVersionToDnsFamily(GetParam()), max_hosts, max_pending_requests, filename);

    TestUtility::loadFromYaml(cluster_type_config, *cluster_.mutable_cluster_type());
    cluster_.mutable_circuit_breakers()
        ->add_thresholds()
        ->mutable_max_pending_requests()
        ->set_value(max_pending_requests);

    // Load the CDS cluster and wait for it to initialize.
    cds_helper_.setCds({cluster_});
    HttpIntegrationTest::initialize();
    test_server_->waitForCounterEq("cluster_manager.cluster_added", 1);
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  }

  void createUpstreams() override {
    if (upstream_tls_) {
      addFakeUpstream(Ssl::createFakeUpstreamSslContext(upstream_cert_name_, context_manager_,
                                                        factory_context_),
                      Http::CodecType::HTTP1);
    } else {
      HttpIntegrationTest::createUpstreams();
    }
    if (use_cache_file_) {
      cache_file_value_contents_ +=
          absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":",
                       fake_upstreams_[0]->localAddress()->ip()->port(), "|1000000|0");
      std::string host =
          fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port());
      TestEnvironment::writeStringToFileForTest("dns_cache.txt",
                                                absl::StrCat(host.length(), "\n", host,
                                                             cache_file_value_contents_.length(),
                                                             "\n", cache_file_value_contents_));
    }
  }

  bool upstream_tls_{};
  std::string upstream_cert_name_{"upstreamlocalhost"};
  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
  std::string cache_file_value_contents_;
  bool use_cache_file_{};
};

class ProxyFilterWithSimtimeIntegrationTest : public Event::TestUsingSimulatedTime,
                                              public ProxyFilterIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyFilterWithSimtimeIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);
INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// A basic test where we pause a request to lookup localhost, and then do another request which
// should hit the TLS cache.
TEST_P(ProxyFilterIntegrationTest, RequestWithBody) {
  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
  // Make sure dns timings are tracked for cache-misses.
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("dns_start")).empty());
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("dns_end")).empty());

  // Now send another request. This should hit the DNS cache.
  response = sendRequestAndWaitForResponse(request_headers, 512, default_response_headers_, 512);
  checkSimpleRequestSuccess(512, 512, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
  // Make sure dns timings are tracked for cache-hits.
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("dns_start")).empty());
  ASSERT_FALSE(response->headers().get(Http::LowerCaseString("dns_end")).empty());
}

// Currently if the first DNS resolution fails, the filter will continue with
// a null address. Make sure this mode fails gracefully.
TEST_P(ProxyFilterIntegrationTest, RequestWithUnknownDomain) {
  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "doesnotexist.example.com"}};

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// Verify that after we populate the cache and reload the cluster we reattach to the cache with
// its existing hosts.
TEST_P(ProxyFilterIntegrationTest, ReloadClusterAndAttachToCache) {
  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());

  // Cause a cluster reload via CDS.
  cluster_.mutable_circuit_breakers()->add_thresholds()->mutable_max_connections()->set_value(100);
  cds_helper_.setCds({cluster_});
  test_server_->waitForCounterEq("cluster_manager.cluster_modified", 1);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);

  // We need to wait until the workers have gotten the new cluster update. The only way we can
  // know this currently is when the connection pools drain and terminate.
  AssertionResult result = fake_upstream_connection_->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
  fake_upstream_connection_.reset();

  // Now send another request. This should hit the DNS cache.
  response = sendRequestAndWaitForResponse(request_headers, 512, default_response_headers_, 512);
  checkSimpleRequestSuccess(512, 512, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
}

// Verify that we expire hosts.
TEST_P(ProxyFilterWithSimtimeIntegrationTest, RemoveHostViaTTL) {
  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
  EXPECT_EQ(1, test_server_->gauge("dns_cache.foo.num_hosts")->value());
  cleanupUpstreamAndDownstream();

  // > 5m
  simTime().advanceTimeWait(std::chrono::milliseconds(300001));
  test_server_->waitForGaugeEq("dns_cache.foo.num_hosts", 0);
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_removed")->value());
}

// Test DNS cache host overflow.
TEST_P(ProxyFilterIntegrationTest, DNSCacheHostOverflow) {
  initializeWithArgs(1);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());

  // Send another request, this should lead to a response directly from the filter.
  const Http::TestRequestHeaderMapImpl request_headers2{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", fmt::format("localhost2", fake_upstreams_[0]->localAddress()->ip()->port())}};
  response = codec_client_->makeHeaderOnlyRequest(request_headers2);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_overflow")->value());
}

// Verify that upstream TLS works with auto verification for SAN as well as auto setting SNI.
TEST_P(ProxyFilterIntegrationTest, UpstreamTls) {
  upstream_tls_ = true;
  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ("localhost", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  checkSimpleRequestSuccess(0, 0, response.get());
}

// Verify that `override_auto_sni_header` can be used along with auto_sni to set
// SNI from an arbitrary header.
TEST_P(ProxyFilterIntegrationTest, UpstreamTlsWithAltHeaderSni) {
  upstream_tls_ = true;
  initializeWithArgs(1024, 1024, "x-host");
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("{}:{}", fake_upstreams_[0]->localAddress()->ip()->addressAsString().c_str(),
                   fake_upstreams_[0]->localAddress()->ip()->port())},
      {"x-host", "localhost"}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ("localhost", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  checkSimpleRequestSuccess(0, 0, response.get());
}

TEST_P(ProxyFilterIntegrationTest, UpstreamTlsWithIpHost) {
  upstream_tls_ = true;
  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", fake_upstreams_[0]->localAddress()->asString()}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // No SNI for IP hosts.
  const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
      dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
          fake_upstream_connection_->connection().ssl().get());
  EXPECT_STREQ(nullptr, SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  checkSimpleRequestSuccess(0, 0, response.get());
}

// Verify that auto-SAN verification fails with an incorrect certificate.
TEST_P(ProxyFilterIntegrationTest, UpstreamTlsInvalidSAN) {
  upstream_tls_ = true;
  upstream_cert_name_ = "upstream";
  initializeWithArgs();
  fake_upstreams_[0]->setReadDisableOnNewConnection(false);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ssl.fail_verify_san")->value());
}

TEST_P(ProxyFilterIntegrationTest, DnsCacheCircuitBreakersInvoked) {
  initializeWithArgs(1024, 0);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response = codec_client_->makeRequestWithBody(request_headers, 1024);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_rq_pending_overflow")->value());

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
}

TEST_P(ProxyFilterIntegrationTest, UseCacheFile) {
  use_cache_file_ = true;

  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  std::string host = fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", host}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.cache_load")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_http1_total")->value());
}

TEST_P(ProxyFilterIntegrationTest, UseCacheFileAndTestHappyEyeballs) {
  autonomous_upstream_ = true;

  config_helper_.addRuntimeOverride("envoy.reloadable_features.allow_multiple_dns_addresses",
                                    "true");
  use_cache_file_ = true;
  // Prepend a bad address
  if (GetParam() == Network::Address::IpVersion::v4) {
    cache_file_value_contents_ = "99.99.99.99:1|1000000|0\n";
  } else {
    cache_file_value_contents_ = "[::99]:1|1000000|0\n";
  }

  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  std::string host = fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", host}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Wait for the request to be received.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 1);
  EXPECT_TRUE(response->waitForEndStream());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.cache_load")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
}

TEST_P(ProxyFilterIntegrationTest, MultipleRequestsLowStreamLimit) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);

  // Ensure we only have one connection upstream, one request active at a time.
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    envoy::config::bootstrap::v3::Bootstrap::StaticResources* static_resources =
        bootstrap.mutable_static_resources();
    envoy::config::cluster::v3::Cluster* cluster = static_resources->mutable_clusters(0);
    envoy::config::cluster::v3::CircuitBreakers* circuit_breakers =
        cluster->mutable_circuit_breakers();
    circuit_breakers->add_thresholds()->mutable_max_connections()->set_value(1);
    ConfigHelper::HttpProtocolOptions protocol_options;
    protocol_options.mutable_explicit_http_config()
        ->mutable_http2_protocol_options()
        ->mutable_max_concurrent_streams()
        ->set_value(1);
    ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                     protocol_options);
  });

  // Start sending the request, but ensure no end stream will be sent, so the
  // stream will stay in use.
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start sending the request, but ensure no end stream will be sent, so the
  // stream will stay in use.
  std::pair<Http::RequestEncoder&, IntegrationStreamDecoderPtr> encoder_decoder =
      codec_client_->startRequest(default_request_headers_);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  // Make sure the headers are received.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());

  // Start another request.
  IntegrationStreamDecoderPtr response2 =
      codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  test_server_->waitForCounterEq("http.config_test.downstream_rq_total", 2);
  // Make sure the stream is not received.
  ASSERT_FALSE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_,
                                                           std::chrono::milliseconds(100)));

  // Finish the first stream.
  codec_client_->sendData(*request_encoder_, 0, true);
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // This should allow the second stream to complete
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());
}

} // namespace
} // namespace Envoy
