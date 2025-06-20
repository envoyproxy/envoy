#include <cassert>
#include <cstdint>
#include <string>

#include "envoy/extensions/clusters/dns_srv/v3/cluster.pb.h"

#include "source/extensions/clusters/dns_srv/dns_srv_cluster.h"

#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/registry.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {

class DnsSrvClusterIntegrationTest
    : public testing::TestWithParam<Envoy::Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DnsSrvClusterIntegrationTest()
      : HttpIntegrationTest(Envoy::Http::CodecType::HTTP1, GetParam()),
        registered_dns_factory_(dns_resolver_factory_) {}

  // void initialize() override {
  // setUpstreamCount(num_upstreams_);
  // setDeterministicValue();
  // config_helper_.renameListener("redis_proxy");

  // Change the port for each of the discovery host in cluster_0.
  // config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  //   uint32_t upstream_idx = 0;
  //   auto* cluster_0 = bootstrap.mutable_static_resources()->mutable_clusters(0);
  //   if (version_ == Network::Address::IpVersion::v4) {
  //     cluster_0->set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V4_ONLY);
  //   }
  //   for (int j = 0; j < cluster_0->load_assignment().endpoints_size(); ++j) {
  //     auto locality_lb = cluster_0->mutable_load_assignment()->mutable_endpoints(j);
  //     for (int k = 0; k < locality_lb->lb_endpoints_size(); ++k) {
  //       auto lb_endpoint = locality_lb->mutable_lb_endpoints(k);
  //       if (lb_endpoint->endpoint().address().has_socket_address()) {
  //         auto* host_socket_addr =
  //             lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
  //         RELEASE_ASSERT(fake_upstreams_.size() > upstream_idx, "");
  //         host_socket_addr->set_address(
  //             fake_upstreams_[upstream_idx]->localAddress()->ip()->addressAsString());
  //         host_socket_addr->set_port_value(
  //             fake_upstreams_[upstream_idx++]->localAddress()->ip()->port());
  //       }
  //     }
  //   }
  // });

  // on_server_ready_function_ = [this](Envoy::IntegrationTestServer& test_server) {
  //   mock_rng_ = dynamic_cast<Random::MockRandomGenerator*>(
  //       &(test_server.server().api().randomGenerator()));
  //   // Abort now if we cannot downcast the server's random number generator pointer.
  //   ASSERT_TRUE(mock_rng_ != nullptr);
  //   // Ensure that fake_upstreams_[0] is the load balancer's host of choice by default.
  //   ON_CALL(*mock_rng_, random()).WillByDefault(Return(random_index_));
  // };

  // HttpIntegrationTest::initialize();
  // }
  void createUpstreams() override { HttpIntegrationTest::createUpstreams(); }

  NiceMock<Envoy::Network::MockDnsResolverFactory> dns_resolver_factory_;
  Envoy::Registry::InjectFactory<Envoy::Network::DnsResolverFactory> registered_dns_factory_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DnsSrvClusterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DnsSrvClusterIntegrationTest, BasicDnsSrvClusterTest) {

  auto dns_resolver = std::make_shared<Network::MockDnsResolver>();
  EXPECT_CALL(dns_resolver_factory_, createDnsResolver(_, _, _))
      .WillRepeatedly(testing::Return(dns_resolver));

  EXPECT_CALL(*dns_resolver, resolve(_, _, _))
      .WillRepeatedly(
          Invoke([this](const std::string& dns_name, Network::DnsLookupFamily,
                        Network::DnsResolver::ResolveCb dns_callback) -> Network::ActiveDnsQuery* {
            ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::testing), debug,
                                "DNS record request for {}", dns_name);

            std::string ip_value = "127.0.0.1";
            if (version_ == Network::Address::IpVersion::v6) {
              ip_value = "::1";
            }

            dns_callback(Network::DnsResolver::ResolutionStatus::Completed, "test resolve: success",
                         TestUtility::makeDnsResponse({ip_value}));
            return nullptr;
          }));

  EXPECT_CALL(*dns_resolver, resolveSrv(_, _))
      .WillRepeatedly(
          Invoke([this](const std::string& dns_name,
                        Network::DnsResolver::ResolveCb dns_callback) -> Network::ActiveDnsQuery* {
            uint16_t port = 0;
            for (const auto& p : fake_upstreams_) {
              port = p->localAddress()->ip()->port();
            }
            assert(port != 0);

            ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::testing), debug,
                                "SRV record request, resolving {} as localhost:{}", dns_name, port);

            std::list<Network::DnsResponse> ret;
            ret.emplace_back(
                Network::DnsResponse(1, 1, port, "localhost", std::chrono::seconds(60)));

            dns_callback(Network::DnsResolver::ResolutionStatus::Completed,
                         "test resolve srv: success", std::move(ret));

            return nullptr;
          }));

  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");
    auto& cluster = *bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster.set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
    cluster.mutable_cluster_type()->set_name("envoy.clusters.dns_srv");
    envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig dns_srv_cluster{};

    dns_srv_cluster.set_srv_name("_local_service._tcp.service.consul.");
    cluster.mutable_cluster_type()->mutable_typed_config()->PackFrom(dns_srv_cluster);

    cluster.mutable_typed_dns_resolver_config()->set_name("envoy.network.dns_resolver.cares");

    cluster.mutable_typed_dns_resolver_config()->mutable_typed_config()->PackFrom(
        envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig());
  });

  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 10000);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
