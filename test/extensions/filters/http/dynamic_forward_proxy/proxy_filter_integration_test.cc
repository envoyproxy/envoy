#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"

using testing::HasSubstr;

namespace Envoy {
namespace {

class ProxyFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  ProxyFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    upstream_tls_ = true;
    filename_ = TestEnvironment::temporaryPath("dns_cache.txt");
    ::unlink(filename_.c_str());
    key_value_config_ = fmt::format(R"EOF(
    key_value_config:
      config:
        name: envoy.key_value.file_based
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.key_value.file_based.v3.FileBasedKeyValueStoreConfig
          filename: {})EOF",
                                    filename_);
  }

  void initializeWithArgs(uint64_t max_hosts = 1024, uint32_t max_pending_requests = 1024,
                          const std::string& override_auto_sni_header = "",
                          const std::string& typed_dns_resolver_config = "",
                          bool use_sub_cluster = false) {
    setUpstreamProtocol(Http::CodecType::HTTP1);

    const std::string filter_use_sub_cluster = R"EOF(
name: dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
  sub_cluster_config:
    cluster_init_timeout: 5s
)EOF";
    const std::string filter_use_dns_cache =
        fmt::format(R"EOF(
name: dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    dns_cache_circuit_breaker:
      max_pending_requests: {}{}{}
)EOF",
                    Network::Test::ipVersionToDnsFamily(GetParam()), max_hosts,
                    max_pending_requests, key_value_config_, typed_dns_resolver_config);
    config_helper_.prependFilter(use_sub_cluster ? filter_use_sub_cluster : filter_use_dns_cache);

    config_helper_.prependFilter(fmt::format(R"EOF(
name: stream-info-to-headers-filter
)EOF"));
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Switch predefined cluster_0 to CDS filesystem sourcing.
      bootstrap.mutable_dynamic_resources()->mutable_cds_config()->set_resource_api_version(
          envoy::config::core::v3::ApiVersion::V3);
      bootstrap.mutable_dynamic_resources()
          ->mutable_cds_config()
          ->mutable_path_config_source()
          ->set_path(cds_helper_.cdsPath());
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
    cluster_.set_dns_lookup_family(
        GetParam() == Network::Address::IpVersion::v4
            ? envoy::config::cluster::v3::Cluster_DnsLookupFamily::Cluster_DnsLookupFamily_V4_ONLY
            : envoy::config::cluster::v3::Cluster_DnsLookupFamily::Cluster_DnsLookupFamily_V6_ONLY);

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

    const std::string cluster_type_config_use_sub_cluster = fmt::format(
        R"EOF(
name: envoy.clusters.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
  sub_clusters_config:
    max_sub_clusters: {}
)EOF",
        max_hosts);
    const std::string cluster_type_config_use_dns_cache = fmt::format(
        R"EOF(
name: envoy.clusters.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    dns_cache_circuit_breaker:
      max_pending_requests: {}{}{}
)EOF",
        Network::Test::ipVersionToDnsFamily(GetParam()), max_hosts, max_pending_requests,
        key_value_config_, typed_dns_resolver_config);

    TestUtility::loadFromYaml(use_sub_cluster ? cluster_type_config_use_sub_cluster
                                              : cluster_type_config_use_dns_cache,
                              *cluster_.mutable_cluster_type());
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
                      Http::CodecType::HTTP1, /*autonomous_upstream=*/false);
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

  void testConnectionTiming(IntegrationStreamDecoderPtr& response, bool cached_dns,
                            int64_t original_usec);

  // Send bidirectional data for CONNECT termination with dynamic forward proxy test.
  void sendBidirectionalData(FakeRawConnectionPtr& fake_raw_upstream_connection,
                             IntegrationStreamDecoderPtr& response,
                             const char* downstream_send_data = "hello",
                             const char* upstream_received_data = "hello",
                             const char* upstream_send_data = "there!",
                             const char* downstream_received_data = "there!") {
    // Send some data upstream.
    codec_client_->sendData(*request_encoder_, downstream_send_data, false);
    ASSERT_TRUE(fake_raw_upstream_connection->waitForData(
        FakeRawConnection::waitForInexactMatch(upstream_received_data)));
    // Send some data downstream.
    ASSERT_TRUE(fake_raw_upstream_connection->write(upstream_send_data));
    response->waitForBodyData(strlen(downstream_received_data));
    EXPECT_EQ(downstream_received_data, response->body());
  }

  void requestWithBodyTest(const std::string& typed_dns_resolver_config = "") {
    int64_t original_usec = dispatcher_->timeSource().monotonicTime().time_since_epoch().count();

    config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

    initializeWithArgs(1024, 1024, "", typed_dns_resolver_config);
    codec_client_ = makeHttpConnection(lookupPort("http"));
    default_request_headers_.setHost(
        fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port()));

    auto response = sendRequestAndWaitForResponse(default_request_headers_, 1024,
                                                  default_response_headers_, 1024);
    checkSimpleRequestSuccess(1024, 1024, response.get());
    testConnectionTiming(response, false, original_usec);
    EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
    EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());

    // Now send another request. This should hit the DNS cache.
    response = sendRequestAndWaitForResponse(default_request_headers_, 512,
                                             default_response_headers_, 512);
    checkSimpleRequestSuccess(512, 512, response.get());
    testConnectionTiming(response, true, original_usec);
    EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
    EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
    // Make sure dns timings are tracked for cache-hits.
    ASSERT_FALSE(response->headers().get(Http::LowerCaseString("dns_start")).empty());
    ASSERT_FALSE(response->headers().get(Http::LowerCaseString("dns_end")).empty());

    if (upstream_tls_) {
      const Extensions::TransportSockets::Tls::SslHandshakerImpl* ssl_socket =
          dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
              fake_upstream_connection_->connection().ssl().get());
      EXPECT_STREQ("localhost", SSL_get_servername(ssl_socket->ssl(), TLSEXT_NAMETYPE_host_name));
    }
  }

  void requestWithUnknownDomainTest(const std::string& typed_dns_resolver_config = "",
                                    const std::string& hostname = "doesnotexist.example.com") {
    useAccessLog("%RESPONSE_CODE_DETAILS%");
    initializeWithArgs(1024, 1024, "", typed_dns_resolver_config);
    codec_client_ = makeHttpConnection(lookupPort("http"));
    default_request_headers_.setHost(hostname);

    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("503", response->headers().getStatusValue());
    EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("dns_resolution_failure"));

    response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("503", response->headers().getStatusValue());
    EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("dns_resolution_failure"));
  }

  bool upstream_tls_{};
  std::string upstream_cert_name_{"upstreamlocalhost"};
  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
  std::string cache_file_value_contents_;
  bool use_cache_file_{};
  std::string filename_;
  std::string key_value_config_;
};

int64_t getHeaderValue(const Http::ResponseHeaderMap& headers, absl::string_view name) {
  EXPECT_FALSE(headers.get(Http::LowerCaseString(name)).empty()) << "Missing " << name;
  int64_t val;
  if (!headers.get(Http::LowerCaseString(name)).empty() &&
      absl::SimpleAtoi(headers.get(Http::LowerCaseString(name))[0]->value().getStringView(),
                       &val)) {
    return val;
  }
  return 0;
}

void ProxyFilterIntegrationTest::testConnectionTiming(IntegrationStreamDecoderPtr& response,
                                                      bool cached_dns, int64_t original_usec) {
  int64_t handshake_end;
  int64_t dns_start = getHeaderValue(response->headers(), "dns_start");
  int64_t dns_end = getHeaderValue(response->headers(), "dns_end");
  int64_t connect_start = getHeaderValue(response->headers(), "upstream_connect_start");
  int64_t connect_end = getHeaderValue(response->headers(), "upstream_connect_complete");
  int64_t request_send_end = getHeaderValue(response->headers(), "request_send_end");
  int64_t response_begin = getHeaderValue(response->headers(), "response_begin");
  Event::DispatcherImpl dispatcher("foo", *api_, timeSystem());

  ASSERT_LT(original_usec, dns_start);
  ASSERT_LE(dns_start, dns_end);
  if (cached_dns) {
    ASSERT_GE(dns_end, connect_start);
  } else {
    ASSERT_LE(dns_end, connect_start);
  }
  if (upstream_tls_) {
    handshake_end = getHeaderValue(response->headers(), "upstream_handshake_complete");
    ASSERT_LE(connect_end, handshake_end);
    ASSERT_LE(handshake_end, request_send_end);
    ASSERT_LT(handshake_end, timeSystem().monotonicTime().time_since_epoch().count());
  }
  ASSERT_LE(connect_start, connect_end);
  ASSERT_LE(request_send_end, response_begin);
}

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
TEST_P(ProxyFilterIntegrationTest, RequestWithBody) { requestWithBodyTest(); }

TEST_P(ProxyFilterIntegrationTest, MultiPortTest) {
  upstream_tls_ = false;
  requestWithBodyTest();

  // Create a second upstream, and send a request there.
  // The second upstream is autonomous where the first was not so we'll only get a 200-ok if we hit
  // the new port.
  // this regression tests https://github.com/envoyproxy/envoy/issues/27331
  autonomous_upstream_ = true;
  createUpstream(Network::Test::getCanonicalLoopbackAddress(version_), upstreamConfig());
  default_request_headers_.setHost(
      fmt::format("localhost:{}", fake_upstreams_[1]->localAddress()->ip()->port()));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Do a sanity check using the getaddrinfo() resolver.
TEST_P(ProxyFilterIntegrationTest, RequestWithBodyGetAddrInfoResolver) {
  // getaddrinfo() does not reliably return v6 addresses depending on the environment. For now
  // just run this on v4 which is most likely to succeed. In v6 only environments this test won't
  // run at all but should still be covered in public CI.
  if (GetParam() != Network::Address::IpVersion::v4) {
    return;
  }

  // See https://github.com/envoyproxy/envoy/issues/28504.
  DISABLE_UNDER_WINDOWS;

  requestWithBodyTest(R"EOF(
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.getaddrinfo
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig)EOF");
}

// Currently if the first DNS resolution fails, the filter will continue with
// a null address. Make sure this mode fails gracefully.
TEST_P(ProxyFilterIntegrationTest, RequestWithUnknownDomain) { requestWithUnknownDomainTest(); }

// TODO(yanavlasov) Enable per #26642
#ifndef ENVOY_ENABLE_UHV
TEST_P(ProxyFilterIntegrationTest, RequestWithSuspectDomain) {
  requestWithUnknownDomainTest("", "\x00\x00.google.com");
}
#endif

// Do a sanity check using the getaddrinfo() resolver.
TEST_P(ProxyFilterIntegrationTest, RequestWithUnknownDomainGetAddrInfoResolver) {
  requestWithUnknownDomainTest(R"EOF(
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.getaddrinfo
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig)EOF");
}

TEST_P(ProxyFilterIntegrationTest, RequestWithUnknownDomainAndNoCaching) {
  key_value_config_ = "";

  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "doesnotexist.example.com"}};

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("dns_resolution_failure"));

  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("dns_resolution_failure"));
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

// Verify that the filter works without TLS.
TEST_P(ProxyFilterIntegrationTest, UpstreamCleartext) {
  upstream_tls_ = false;
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

  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  checkSimpleRequestSuccess(0, 0, response.get());
}

// Regression test a bug where the host header was used for cache lookups rather than host:port key
TEST_P(ProxyFilterIntegrationTest, CacheSansPort) {
  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "localhost"}};

  // Send a request to localhost, with no port specified. The cluster will
  // default to localhost:443, and the connection will fail.
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              HasSubstr("upstream_reset_before_response_started"));
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());

  // Now try a second request and make sure it encounters the same error rather
  // than dns_resolution_failure.
  auto response2 = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("503", response2->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1),
              HasSubstr("upstream_reset_before_response_started"));
}

// Verify that `override_auto_sni_header` can be used along with auto_sni to set
// SNI from an arbitrary header.
TEST_P(ProxyFilterIntegrationTest, UpstreamTlsWithAltHeaderSni) {
  upstream_tls_ = true;
  initializeWithArgs(1024, 1024, "x-host");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  std::string authority;
  if (GetParam() == Network::Address::IpVersion::v6) {
    authority =
        fmt::format("[{}]:{}", fake_upstreams_[0]->localAddress()->ip()->addressAsString().c_str(),
                    fake_upstreams_[0]->localAddress()->ip()->port());
  } else {
    authority =
        fmt::format("{}:{}", fake_upstreams_[0]->localAddress()->ip()->addressAsString().c_str(),
                    fake_upstreams_[0]->localAddress()->ip()->port());
  }

  const Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", authority},
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
  upstream_tls_ = false; // upstream creation doesn't handle autonomous_upstream_
  autonomous_upstream_ = true;

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
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
  EXPECT_TRUE(response->waitForEndStream());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.cache_load")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
}

TEST_P(ProxyFilterIntegrationTest, MultipleRequestsLowStreamLimit) {
  upstream_tls_ = false; // config below uses bootstrap, tls config is in cluster_

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

// Test Envoy CONNECT request termination works with dynamic forward proxy config.
TEST_P(ProxyFilterIntegrationTest, ConnectRequestWithDFPConfig) {
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { ConfigHelper::setConnectConfig(hcm, true, false); });

  enableHalfClose(true);
  initializeWithArgs();

  const Http::TestRequestHeaderMapImpl connect_headers{
      {":method", "CONNECT"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  FakeRawConnectionPtr fake_raw_upstream_connection;
  IntegrationStreamDecoderPtr response;
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(connect_headers);
  request_encoder_ = &encoder_decoder.first;
  response = std::move(encoder_decoder.second);
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_raw_upstream_connection));
  response->waitForHeaders();

  sendBidirectionalData(fake_raw_upstream_connection, response, "hello", "hello", "there!",
                        "there!");
  // Send a second set of data to make sure for example headers are only sent once.
  sendBidirectionalData(fake_raw_upstream_connection, response, ",bye", "hello,bye", "ack",
                        "there!ack");

  // Send an end stream. This should result in half close upstream.
  codec_client_->sendData(*request_encoder_, "", true);
  ASSERT_TRUE(fake_raw_upstream_connection->waitForHalfClose());
  // Now send a FIN from upstream. This should result in clean shutdown downstream.
  ASSERT_TRUE(fake_raw_upstream_connection->close());
  ASSERT_TRUE(codec_client_->waitForDisconnect());
}

// Make sure if there are more HTTP/1.1 streams in flight than connections allowed by cluster
// circuit breakers, that excess streams are queued.
TEST_P(ProxyFilterIntegrationTest, TestQueueingBasedOnCircuitBreakers) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP1);
  upstream_tls_ = false; // config below uses bootstrap, tls config is in cluster_
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* static_resources = bootstrap.mutable_static_resources();
    for (int i = 0; i < static_resources->clusters_size(); ++i) {
      auto* cluster = static_resources->mutable_clusters(i);
      auto* per_host_thresholds = cluster->mutable_circuit_breakers()->add_per_host_thresholds();
      per_host_thresholds->mutable_max_connections()->set_value(1);
    }
  });

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
  // Make sure the stream is received, but no new connection is established.
  test_server_->waitForCounterEq("http.config_test.downstream_rq_total", 2);
  EXPECT_EQ(1, test_server_->gauge("cluster.cluster_0.upstream_cx_active")->value());

  // Finish the first stream.
  codec_client_->sendData(*request_encoder_, 0, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // This should allow the second stream to complete on the original connection.
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForHeadersComplete());
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_TRUE(response2->complete());
  EXPECT_EQ("200", response2->headers().getStatusValue());
}

TEST_P(ProxyFilterIntegrationTest, SubClusterWithUnknownDomain) {
  key_value_config_ = "";

  useAccessLog("%RESPONSE_CODE_DETAILS%");
  initializeWithArgs(1024, 1024, "", "", true);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "doesnotexist.example.com"}};

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("no_healthy_upstream"));

  response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_, 1), HasSubstr("no_healthy_upstream"));
}

// Verify that removed all sub cluster when dfp cluster is removed/updated.
TEST_P(ProxyFilterIntegrationTest, SubClusterReloadCluster) {
  initializeWithArgs(1024, 1024, "", "", true);
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
  // one more sub cluster
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 2);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForCounterEq("cluster_manager.cluster_modified", 0);
  test_server_->waitForCounterEq("cluster_manager.cluster_removed", 0);

  // Cause a cluster reload via CDS.
  cluster_.mutable_circuit_breakers()->add_thresholds()->mutable_max_connections()->set_value(100);
  cds_helper_.setCds({cluster_});
  // sub cluster is removed
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 2);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForCounterEq("cluster_manager.cluster_modified", 1);
  test_server_->waitForCounterEq("cluster_manager.cluster_removed", 1);

  // We need to wait until the workers have gotten the new cluster update. The only way we can
  // know this currently is when the connection pools drain and terminate.
  AssertionResult result = fake_upstream_connection_->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());
  fake_upstream_connection_.reset();

  // Now send another request. This should create a new sub cluster.
  response = sendRequestAndWaitForResponse(request_headers, 512, default_response_headers_, 512);
  checkSimpleRequestSuccess(512, 512, response.get());
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 3);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
}

// Verify that we expire sub clusters.
TEST_P(ProxyFilterWithSimtimeIntegrationTest, RemoveSubClusterViaTTL) {
  initializeWithArgs(1024, 1024, "", "", true);
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
  // one more cluster
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 2);
  test_server_->waitForCounterEq("cluster_manager.cluster_removed", 0);
  cleanupUpstreamAndDownstream();

  // > 5m
  simTime().advanceTimeWait(std::chrono::milliseconds(300001));
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 2);
  test_server_->waitForCounterEq("cluster_manager.cluster_removed", 1);
}

// Test sub clusters overflow.
TEST_P(ProxyFilterIntegrationTest, SubClusterOverflow) {
  initializeWithArgs(1, 1024, "", "", true);

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
}

TEST_P(ProxyFilterIntegrationTest, SubClusterWithIpHost) {
  upstream_tls_ = true;
  initializeWithArgs(1024, 1024, "", "", true);
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

} // namespace
} // namespace Envoy
