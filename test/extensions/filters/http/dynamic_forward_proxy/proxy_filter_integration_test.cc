#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/ssl_socket.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/filters/http/dynamic_forward_proxy/test_resolver.h"
#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/registry.h"

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
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  void initialize() override { initializeWithArgs(); }

  void initializeWithArgs(uint64_t max_hosts = 1024, uint32_t max_pending_requests = 1024,
                          const std::string& override_auto_sni_header = "",
                          const std::string& typed_dns_resolver_config = "",
                          bool use_sub_cluster = false, double dns_query_timeout = 5) {
    const std::string filter_use_sub_cluster = R"EOF(
name: dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
  sub_cluster_config:
    cluster_init_timeout: 5s
)EOF";
    const std::string filter_use_dns_cache = fmt::format(
        R"EOF(
name: dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
  dns_cache_config:
    dns_min_refresh_rate: 1s
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    host_ttl: {}s
    dns_query_timeout: {:.9f}s
    dns_cache_circuit_breaker:
      max_pending_requests: {}{}{}
)EOF",
        Network::Test::ipVersionToDnsFamily(GetParam()), max_hosts, host_ttl_, dns_query_timeout,
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

    protocol_options_.mutable_upstream_http_protocol_options()->set_auto_sni(true);
    if (!override_auto_sni_header.empty()) {
      protocol_options_.mutable_upstream_http_protocol_options()->set_override_auto_sni_header(
          override_auto_sni_header);
    }
    protocol_options_.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
    if (upstreamProtocol() == Http::CodecType::HTTP1) {
      protocol_options_.mutable_explicit_http_config()->mutable_http_protocol_options();
      ConfigHelper::setProtocolOptions(cluster_, protocol_options_);
    } else if (upstreamProtocol() == Http::CodecType::HTTP2) {
      protocol_options_.mutable_explicit_http_config()->mutable_http2_protocol_options();
      if (low_stream_limits_) {
        protocol_options_.mutable_explicit_http_config()
            ->mutable_http2_protocol_options()
            ->mutable_max_concurrent_streams()
            ->set_value(1);
      }
      ConfigHelper::setProtocolOptions(cluster_, protocol_options_);
    } else {
      ASSERT(!low_stream_limits_);
      // H3 config is set below after fake upstream creation.
    }

    if (low_stream_limits_) {
      envoy::config::cluster::v3::CircuitBreakers* circuit_breakers =
          cluster_.mutable_circuit_breakers();
      circuit_breakers->add_thresholds()->mutable_max_connections()->set_value(1);
    }

    if (upstream_tls_) {
      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
      auto* validation_context =
          tls_context.mutable_common_tls_context()->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
      if (upstreamProtocol() != Http::CodecType::HTTP3) {
        cluster_.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
        cluster_.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
      } else {
        envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport quic_context;
        quic_context.mutable_upstream_tls_context()->CopyFrom(tls_context);
        cluster_.mutable_transport_socket()->set_name("envoy.transport_sockets.quic");
        cluster_.mutable_transport_socket()->mutable_typed_config()->PackFrom(quic_context);
      }
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
    dns_min_refresh_rate: 1s
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
    host_ttl: {}s
    dns_query_timeout: {:.9f}s
    dns_cache_circuit_breaker:
      max_pending_requests: {}{}{}
)EOF",
        Network::Test::ipVersionToDnsFamily(GetParam()), max_hosts, host_ttl_, dns_query_timeout,
        max_pending_requests, key_value_config_, typed_dns_resolver_config);

    TestUtility::loadFromYaml(use_sub_cluster ? cluster_type_config_use_sub_cluster
                                              : cluster_type_config_use_dns_cache,
                              *cluster_.mutable_cluster_type());
    cluster_.mutable_circuit_breakers()
        ->add_thresholds()
        ->mutable_max_pending_requests()
        ->set_value(max_pending_requests);

    // CDS cluster is loaded in createUpstreams() to handle late H3 information.
    HttpIntegrationTest::initialize();
    test_server_->waitForCounterEq("cluster_manager.cluster_added", 1);
    test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);

    default_request_headers_.setHost(
        fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port()));
  }

  void createUpstreams() override {
    if (upstream_tls_ && !upstream_cert_name_.empty()) {
      addFakeUpstream(Ssl::createFakeUpstreamSslContext(upstream_cert_name_, context_manager_,
                                                        factory_context_),
                      upstreamProtocol(), /*autonomous_upstream=*/false);
    } else {
      HttpIntegrationTest::createUpstreams();
    }
    if (use_cache_file_) {
      std::string address = version_ == Network::Address::IpVersion::v4
                                ? upstream_address_fn_(0)->ip()->addressAsString()
                                : Network::Test::getLoopbackAddressUrlString(version_);
      cache_file_value_contents_ +=
          absl::StrCat(address, ":", fake_upstreams_[0]->localAddress()->ip()->port(), "|",
                       dns_cache_ttl_, "|0");
      std::string host =
          fmt::format("{}:{}", dns_hostname_, fake_upstreams_[0]->localAddress()->ip()->port());
      TestEnvironment::writeStringToFileForTest("dns_cache.txt",
                                                absl::StrCat(host.length(), "\n", host,
                                                             cache_file_value_contents_.length(),
                                                             "\n", cache_file_value_contents_));
    }
    if (upstreamProtocol() == Http::CodecType::HTTP3) {
      protocol_options_.mutable_auto_config()->mutable_http3_protocol_options();
      auto* alt_cache_options =
          protocol_options_.mutable_auto_config()->mutable_alternate_protocols_cache_options();
      alt_cache_options->set_name("default_alternate_protocols_cache");

      alt_cache_options->add_canonical_suffixes(".lyft.com");
      auto* entry = alt_cache_options->add_prepopulated_entries();
      entry->set_hostname("sni.lyft.com");
      entry->set_port(fake_upstreams_[0]->localAddress()->ip()->port());
      ConfigHelper::setProtocolOptions(cluster_, protocol_options_);
    }
    // Load the CDS cluster and wait for it to initialize.
    cds_helper_.setCds({cluster_});
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
    std::string access_log = waitForAccessLog(access_log_name_);
    EXPECT_THAT(access_log, HasSubstr("dns_resolution_failure"));
    EXPECT_FALSE(StringUtil::hasEmptySpace(access_log));

    response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_EQ("503", response->headers().getStatusValue());
    access_log = waitForAccessLog(access_log_name_, 1);
    EXPECT_THAT(access_log, HasSubstr("dns_resolution_failure"));
    EXPECT_FALSE(StringUtil::hasEmptySpace(access_log));
  }

  void multipleRequestsMaybeReresolve(bool reresolve) {
    if (reresolve) {
      config_helper_.addRuntimeOverride("envoy.reloadable_features.reresolve_if_no_connections",
                                        "true");
    }
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));

    // Send the first request / response pair.
    IntegrationStreamDecoderPtr response1 =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response1->waitForEndStream());

    // Close the upstream connection and wait for it to be detected.
    ASSERT_TRUE(fake_upstream_connection_->close());
    fake_upstream_connection_.reset();
    test_server_->waitForCounterEq("cluster.cluster_0.upstream_cx_destroy", 1);

    IntegrationStreamDecoderPtr response2 =
        codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response2->waitForEndStream());
    if (reresolve) {
      EXPECT_EQ(2, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
    } else {
      EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
    }
  }

  bool upstream_tls_{};
  bool low_stream_limits_{};
  std::string upstream_cert_name_{"upstreamlocalhost"};
  CdsHelper cds_helper_;
  envoy::config::cluster::v3::Cluster cluster_;
  ConfigHelper::HttpProtocolOptions protocol_options_;
  std::string cache_file_value_contents_;
  bool use_cache_file_{};
  uint32_t host_ttl_{1};
  uint32_t dns_cache_ttl_{1000000};
  std::string filename_;
  std::string key_value_config_;
  std::string dns_hostname_{"localhost"};
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

TEST_P(ProxyFilterIntegrationTest, GetAddrInfoResolveTimeoutWithTrace) {
  Network::OverrideAddrInfoDnsResolverFactory factory;
  Registry::InjectFactory<Network::DnsResolverFactory> inject_factory(factory);
  Registry::InjectFactory<Network::DnsResolverFactory>::forceAllowDuplicates();
  config_helper_.addRuntimeOverride("envoy.enable_dfp_dns_trace", "true");
  useAccessLog("%RESPONSE_CODE_DETAILS%");

  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);

  config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

  upstream_tls_ = false; // upstream creation doesn't handle autonomous_upstream_
  autonomous_upstream_ = true;
  std::string resolver_config = R"EOF(
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.getaddrinfo
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig)EOF";
  initializeWithArgs(1024, 1024, "", resolver_config, false, 0.000000001);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              HasSubstr("dns_resolution_failure{resolve_timeout:"));
}

TEST_P(ProxyFilterIntegrationTest, GetAddrInfoResolveTimeoutWithoutTrace) {
  Network::OverrideAddrInfoDnsResolverFactory factory;
  Registry::InjectFactory<Network::DnsResolverFactory> inject_factory(factory);
  Registry::InjectFactory<Network::DnsResolverFactory>::forceAllowDuplicates();
  useAccessLog("%RESPONSE_CODE_DETAILS%");

  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);

  config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

  upstream_tls_ = false; // upstream creation doesn't handle autonomous_upstream_
  autonomous_upstream_ = true;
  std::string resolver_config = R"EOF(
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.getaddrinfo
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig)EOF";
  initializeWithArgs(1024, 1024, "", resolver_config, false, 0.000000001);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_),
              HasSubstr("dns_resolution_failure{resolve_timeout}"));
}

TEST_P(ProxyFilterIntegrationTest, ParallelRequests) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);

  config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

  upstream_tls_ = false; // upstream creation doesn't handle autonomous_upstream_
  autonomous_upstream_ = true;
  initializeWithArgs(1024, 1024, "", "");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);

  ASSERT_TRUE(response1->waitForEndStream());
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  EXPECT_EQ("200", response2->headers().getStatusValue());
}

TEST_P(ProxyFilterIntegrationTest, ParallelRequestsWithFakeResolver) {
  Network::OverrideAddrInfoDnsResolverFactory factory;
  Registry::InjectFactory<Network::DnsResolverFactory> inject_factory(factory);
  Registry::InjectFactory<Network::DnsResolverFactory>::forceAllowDuplicates();

  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);

  std::string resolver_config = R"EOF(
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.getaddrinfo
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig)EOF";

  config_helper_.prependFilter(fmt::format(R"EOF(
  name: stream-info-to-headers-filter
)EOF"));

  upstream_tls_ = false;
  autonomous_upstream_ = true;
  initializeWithArgs(1024, 1024, "", resolver_config);
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Kick off the first request.
  auto response1 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  // Wait fo the query to kick off
  test_server_->waitForCounterEq("dns_cache.foo.dns_query_attempt", 1);
  // Start the next request before unblocking the resolve.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  Network::TestResolver::unblockResolve();

  ASSERT_TRUE(response1->waitForEndStream());
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response1->headers().getStatusValue());
  EXPECT_EQ("200", response2->headers().getStatusValue());
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

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  std::string access_log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(access_log, HasSubstr("dns_resolution_failure"));
  EXPECT_FALSE(StringUtil::hasEmptySpace(access_log));

  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  access_log = waitForAccessLog(access_log_name_, 1);
  EXPECT_THAT(access_log, HasSubstr("dns_resolution_failure"));
  EXPECT_FALSE(StringUtil::hasEmptySpace(access_log));
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

TEST_P(ProxyFilterIntegrationTest, UpstreamTlsWithTooLongSni) {
  upstream_tls_ = true;
  initializeWithArgs(1024, 1024, "x-host");
  std::string too_long_sni(300, 'a');
  ASSERT_EQ(too_long_sni.size(), 300); // Validate that the expected constructor was run.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                       {":path", "/test/long/url"},
                                                       {":scheme", "http"},
                                                       {":authority", "localhost"},
                                                       {"x-host", too_long_sni}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  // TODO(ggreenway): validate (in access logs probably) that failure reason is set appropriately.
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

TEST_P(ProxyFilterIntegrationTest, UseCacheFileShortTtl) {
  upstream_tls_ = false; // avoid cert errors for unknown hostname
  use_cache_file_ = true;
  dns_cache_ttl_ = 2;

  dns_hostname_ = "not_actually_localhost"; // Set to a name that won't resolve.
  initializeWithArgs();
  std::string host =
      fmt::format("{}:{}", dns_hostname_, fake_upstreams_[0]->localAddress()->ip()->port());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", host}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.cache_load")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_http1_total")->value());

  // Wait for the host to be removed due to short TTL
  test_server_->waitForCounterGe("dns_cache.foo.host_removed", 1);

  // Send a request and expect an error due to 1) removed host and 2) DNS resolution fail.
  response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
}

// As with UseCacheFileShortTtl set up with a short TTL but make sure the DNS
// resolution failure doesn't result in killing off a stream in progress.
TEST_P(ProxyFilterIntegrationTest, StreamPersistAcrossShortTtlResFail) {
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);

  upstream_tls_ = false; // avoid cert errors for unknown hostname
  use_cache_file_ = true;
  dns_cache_ttl_ = 2;

  dns_hostname_ = "not_actually_localhost"; // Set to a name that won't resolve.
  initializeWithArgs();
  std::string host =
      fmt::format("{}:{}", dns_hostname_, fake_upstreams_[0]->localAddress()->ip()->port());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", host}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // When the TTL is hit, the host will be removed from the DNS cache. This
  // won't break the outstanding connection.
  test_server_->waitForCounterGe("dns_cache.foo.host_removed", 1);

  // Kick off a new request before the first is served.
  auto response2 = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Make sure response 1 is served.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Because request 2 started after DNS entry eviction it will fail due to DNS lookup failure.
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("503", response2->headers().getStatusValue());
}
const BaseIntegrationTest::InstanceConstSharedPtrFn alternateLoopbackFunction() {
  return [](int) { return Network::Utility::parseInternetAddressNoThrow("127.0.0.2", 0); };
}

// Make sure that even with a resolution success we won't drain the connection.
TEST_P(ProxyFilterIntegrationTest, StreamPersistAcrossShortTtlResSuccess) {
  if (version_ != Network::Address::IpVersion::v4) {
    return;
  }
  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);

  host_ttl_ = 8000;
  upstream_tls_ = false; // avoid cert errors for unknown hostname
  use_cache_file_ = true;
  dns_cache_ttl_ = 2;
  // The test will start with the fake upstream latched to 127.0.0.2.
  // Re-resolve will point it at 127.0.0.1
  upstream_address_fn_ = alternateLoopbackFunction();

  initializeWithArgs();
  std::string host =
      fmt::format("{}:{}", dns_hostname_, fake_upstreams_[0]->localAddress()->ip()->port());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", host}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // When the TTL is hit, the host will be removed from the DNS cache. This
  // won't break the outstanding connection.
  test_server_->waitForCounterGe("dns.cares.resolve_total", 1);

  // Kick off a new request before the first is served.
  auto response2 = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Make sure response 1 is served.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // The request will succeed as it will use the prior connection despite the
  // new resolution
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("200", response2->headers().getStatusValue());
}

TEST_P(ProxyFilterIntegrationTest, UseCacheFileShortTtlHostActive) {
  upstream_tls_ = false; // avoid cert errors for unknown hostname
  use_cache_file_ = true;
  dns_cache_ttl_ = 2;

  dns_hostname_ = "not_actually_localhost"; // Set to a name that won't resolve.
  initializeWithArgs();
  std::string host =
      fmt::format("{}:{}", dns_hostname_, fake_upstreams_[0]->localAddress()->ip()->port());
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", host}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  waitForNextUpstreamRequest();

  // Wait for the host to be removed due to short TTL
  test_server_->waitForCounterGe("dns_cache.foo.host_removed", 1);

  // Finish the response.
  upstream_request_->encodeHeaders(default_response_headers_, true);

  // The disconnect will trigger failure.
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
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

#if defined(ENVOY_ENABLE_QUIC)
TEST_P(ProxyFilterIntegrationTest, UseCacheFileAndHttp3) {
  upstream_cert_name_ = ""; // Force standard TLS
  dns_hostname_ = "sni.lyft.com";
  autonomous_upstream_ = true;
  setUpstreamProtocol(Http::CodecType::HTTP3);

  use_cache_file_ = true;
  // Unlike TCP happy eyeballs, HTTP/3 will only work if address families differ.
  // Prepend a bad address with opposite address family.
  if (GetParam() == Network::Address::IpVersion::v6) {
    cache_file_value_contents_ = "99.99.99.99:1|1000000|0\n";
  } else {
    cache_file_value_contents_ = "[::99]:1|1000000|0\n";
  }

  initializeWithArgs();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  std::string host =
      fmt::format("sni.lyft.com:{}", fake_upstreams_[0]->localAddress()->ip()->port());
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", host}};

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);

  // Wait for the request to be received.
  test_server_->waitForCounterEq("cluster.cluster_0.upstream_rq_total", 1);
  EXPECT_TRUE(response->waitForEndStream());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.cache_load")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
}
#endif

TEST_P(ProxyFilterIntegrationTest, MultipleRequestsLowStreamLimit) {
  upstream_tls_ = false; // config below uses bootstrap, tls config is in cluster_
  // Ensure we only have one connection upstream, one request active at a time.
  low_stream_limits_ = true;

  setDownstreamProtocol(Http::CodecType::HTTP2);
  setUpstreamProtocol(Http::CodecType::HTTP2);

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
  test_server_->waitForCounterEq("dns_cache.foo.dns_query_attempt", 1);
}

TEST_P(ProxyFilterIntegrationTest, MultipleRequestsForceRefreshOff) {
  multipleRequestsMaybeReresolve(false);
}

TEST_P(ProxyFilterIntegrationTest, MultipleRequestsForceRefreshOn) {
  multipleRequestsMaybeReresolve(true);
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
  auto* per_host_thresholds = cluster_.mutable_circuit_breakers()->add_per_host_thresholds();
  per_host_thresholds->mutable_max_connections()->set_value(1);

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

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  EXPECT_THAT(waitForAccessLog(access_log_name_), HasSubstr("no_healthy_upstream"));

  response = codec_client_->makeHeaderOnlyRequest(request_headers);
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

// Verify that we expire sub clusters and not remove on CDS.
TEST_P(ProxyFilterWithSimtimeIntegrationTest, RemoveViaTTLAndDFPUpdateWithoutAvoidCDSRemoval) {
  const std::string cluster_yaml = R"EOF(
    name: fake_cluster
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: fake_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";
  auto cluster = Upstream::parseClusterFromV3Yaml(cluster_yaml);
  // make runtime guard false
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.avoid_dfp_cluster_removal_on_cds_update", "false");
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

  // Sub cluster expected to be removed after ttl
  // > 5m
  simTime().advanceTimeWait(std::chrono::milliseconds(300001));
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 2);
  test_server_->waitForCounterEq("cluster_manager.cluster_removed", 1);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  response = sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());

  // sub cluster added again
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 3);
  test_server_->waitForCounterEq("cluster_manager.cluster_removed", 1);
  cleanupUpstreamAndDownstream();

  // Make update to DFP cluster
  cluster_.mutable_circuit_breakers()->add_thresholds()->mutable_max_connections()->set_value(100);
  cds_helper_.setCds({cluster_});

  // sub cluster removed due to dfp cluster update
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 3);
  test_server_->waitForCounterEq("cluster_manager.cluster_removed", 2);
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

// Verify that no DFP clusters are removed when CDS Reload is triggered.
TEST_P(ProxyFilterIntegrationTest, CDSReloadNotRemoveDFPCluster) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.avoid_dfp_cluster_removal_on_cds_update", "true");
  const std::string cluster_yaml = R"EOF(
    name: fake_cluster
    connect_timeout: 0.250s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: fake_cluster
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 11001
  )EOF";
  auto cluster = Upstream::parseClusterFromV3Yaml(cluster_yaml);

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
  cds_helper_.setCds({cluster_, cluster});
  // a new cluster is added and no dfp cluster is removed
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 3);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
  test_server_->waitForCounterEq("cluster_manager.cluster_modified", 0);
  // No DFP cluster should be removed.
  test_server_->waitForCounterEq("cluster_manager.cluster_removed", 0);

  // The fake upstream connection should stay connected
  ASSERT_TRUE(fake_upstream_connection_->connected());

  // Now send another request. This should not create new sub cluster.
  response = sendRequestAndWaitForResponse(request_headers, 512, default_response_headers_, 512);
  checkSimpleRequestSuccess(512, 512, response.get());

  // No new cluster should be added as DFP cluster already exists
  test_server_->waitForCounterEq("cluster_manager.cluster_added", 3);
  test_server_->waitForGaugeEq("cluster_manager.warming_clusters", 0);
}

} // namespace
} // namespace Envoy
