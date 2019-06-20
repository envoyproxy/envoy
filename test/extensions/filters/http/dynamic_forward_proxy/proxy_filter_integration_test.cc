#include "test/integration/http_integration.h"

namespace Envoy {
namespace {

class ProxyFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public Event::TestUsingSimulatedTime,
                                   public HttpIntegrationTest {
public:
  ProxyFilterIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  static std::string ipVersionToDnsFamily(Network::Address::IpVersion version) {
    switch (version) {
    case Network::Address::IpVersion::v4:
      return "V4_ONLY";
    case Network::Address::IpVersion::v6:
      return "V6_ONLY";
    }

    // This seems to be needed on the coverage build for some reason.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  void setup(uint64_t max_hosts = 1024) {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);

    const std::string filter = fmt::format(R"EOF(
name: envoy.filters.http.dynamic_forward_proxy
config:
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
)EOF",
                                           ipVersionToDnsFamily(GetParam()), max_hosts);
    config_helper_.addFilter(filter);

    config_helper_.addConfigModifier(
        [max_hosts](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
          auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster_0->clear_hosts();
          cluster_0->set_lb_policy(envoy::api::v2::Cluster::CLUSTER_PROVIDED);

          const std::string cluster_type_config =
              fmt::format(R"EOF(
name: envoy.clusters.dynamic_forward_proxy
typed_config:
  "@type": type.googleapis.com/envoy.config.cluster.dynamic_forward_proxy.v2alpha.ClusterConfig
  dns_cache_config:
    name: foo
    dns_lookup_family: {}
    max_hosts: {}
)EOF",
                          ipVersionToDnsFamily(GetParam()), max_hosts);

          TestUtility::loadFromYaml(cluster_type_config, *cluster_0->mutable_cluster_type());
        });

    HttpIntegrationTest::initialize();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ProxyFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// A basic test where we pause a request to lookup localhost, and then do another request which
// should hit the TLS cache.
TEST_P(ProxyFilterIntegrationTest, RequestWithBody) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestHeaderMapImpl request_headers{
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

  // Now send another request. This should hit the DNS cache.
  response = sendRequestAndWaitForResponse(request_headers, 512, default_response_headers_, 512);
  checkSimpleRequestSuccess(512, 512, response.get());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.dns_query_attempt")->value());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_added")->value());
}

// Verify that we expire hosts.
TEST_P(ProxyFilterIntegrationTest, RemoveHostViaTTL) {
  setup();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestHeaderMapImpl request_headers{
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

  // > 5m
  simTime().sleep(std::chrono::milliseconds(300001));
  test_server_->waitForGaugeEq("dns_cache.foo.num_hosts", 0);
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_removed")->value());
}

// Test DNS cache host overflow.
TEST_P(ProxyFilterIntegrationTest, DNSCacheHostOverflow) {
  setup(1);

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const Http::TestHeaderMapImpl request_headers{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority",
       fmt::format("localhost:{}", fake_upstreams_[0]->localAddress()->ip()->port())}};

  auto response =
      sendRequestAndWaitForResponse(request_headers, 1024, default_response_headers_, 1024);
  checkSimpleRequestSuccess(1024, 1024, response.get());

  // Send another request, this should lead to a response directly from the filter.
  const Http::TestHeaderMapImpl request_headers2{
      {":method", "POST"},
      {":path", "/test/long/url"},
      {":scheme", "http"},
      {":authority", fmt::format("localhost2", fake_upstreams_[0]->localAddress()->ip()->port())}};
  response = codec_client_->makeHeaderOnlyRequest(request_headers2);
  response->waitForEndStream();
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());
  EXPECT_EQ(1, test_server_->counter("dns_cache.foo.host_overflow")->value());
}

} // namespace
} // namespace Envoy
