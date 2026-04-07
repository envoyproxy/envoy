#include "envoy/extensions/network/dns_resolver/hickory/v3/hickory_dns_resolver.pb.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace Network {
namespace {

class HickoryDnsIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  HickoryDnsIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HickoryDnsIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(HickoryDnsIntegrationTest, LogicalDnsWithHickoryResolver) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.set_type(envoy::config::cluster::v3::Cluster::LOGICAL_DNS);
    cluster.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
    auto* typed_dns_resolver_config = cluster.mutable_typed_dns_resolver_config();
    typed_dns_resolver_config->set_name("envoy.network.dns_resolver.hickory");
    envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig hickory_config;
    typed_dns_resolver_config->mutable_typed_config()->PackFrom(hickory_config);
  });
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) {
        auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
        route->mutable_route()->mutable_auto_host_rewrite()->set_value(true);
      });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(HickoryDnsIntegrationTest, StrictDnsWithHickoryResolver) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.set_type(envoy::config::cluster::v3::Cluster::STRICT_DNS);
    cluster.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
    auto* typed_dns_resolver_config = cluster.mutable_typed_dns_resolver_config();
    typed_dns_resolver_config->set_name("envoy.network.dns_resolver.hickory");
    envoy::extensions::network::dns_resolver::hickory::v3::HickoryDnsResolverConfig hickory_config;
    typed_dns_resolver_config->mutable_typed_config()->PackFrom(hickory_config);
  });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace
} // namespace Network
} // namespace Envoy
