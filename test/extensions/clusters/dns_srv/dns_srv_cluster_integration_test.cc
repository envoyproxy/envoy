#include <cassert>
#include <cstdint>
#include <string>

#include "envoy/extensions/clusters/dns_srv/v3/cluster.pb.h"

#include "source/extensions/clusters/dns_srv/dns_srv_cluster.h"

#include "test/integration/http_integration.h"
#include "test/integration/integration.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {

// SimulatedTimeSystem is listed first so its constructor registers the simulated clock as the
// global time system before HttpIntegrationTest (and BaseIntegrationTest) initialise.
class DnsSrvClusterIntegrationTest
    : public Event::SimulatedTimeSystem,
      public testing::TestWithParam<Envoy::Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  // Must match the dns_refresh_rate configured by addSrvClusterConfigModifier().
  static constexpr std::chrono::milliseconds kDnsRefreshRate{5000};

  DnsSrvClusterIntegrationTest()
      : HttpIntegrationTest(Envoy::Http::CodecType::HTTP1, GetParam()),
        registered_dns_factory_(dns_resolver_factory_) {}

  void createUpstreams() override { HttpIntegrationTest::createUpstreams(); }

  void addSrvClusterConfigModifier() {
    config_helper_.addConfigModifier(
        [](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          ASSERT(bootstrap.mutable_static_resources()->clusters_size() == 1, "");

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
  }

  // Advance simulated time past one refresh period, triggering the next DNS resolution cycle.
  // Blocks until all timer callbacks fired by the time advance have completed.
  void triggerDnsRefresh() {
    Event::SimulatedTimeSystem::timeSystem().advanceTimeWait(kDnsRefreshRate +
                                                             std::chrono::milliseconds(1));
  }

  // Returns the set of ports of all hosts currently in cluster_0.
  std::set<uint16_t> clusterHostPorts() {
    std::set<uint16_t> ports;
    const auto& cluster_map = test_server_->server().clusterManager().clusters();
    const auto& cluster_ref = cluster_map.active_clusters_.find("cluster_0")->second;
    for (const auto& host : cluster_ref.get().prioritySet().hostSetsPerPriority()[0]->hosts()) {
      ports.insert(host->address()->ip()->port());
    }
    return ports;
  }

  // Returns the set of IP address strings of all hosts currently in cluster_0.
  std::set<std::string> clusterHostIps() {
    std::set<std::string> ips;
    const auto& cluster_map = test_server_->server().clusterManager().clusters();
    const auto& cluster_ref = cluster_map.active_clusters_.find("cluster_0")->second;
    for (const auto& host : cluster_ref.get().prioritySet().hostSetsPerPriority()[0]->hosts()) {
      ips.insert(host->address()->ip()->addressAsString());
    }
    return ips;
  }

  NiceMock<Envoy::Network::MockDnsResolverFactory> dns_resolver_factory_;
  Envoy::Registry::InjectFactory<Envoy::Network::DnsResolverFactory> registered_dns_factory_;
};

// A programmable mock DNS resolver whose SRV and A/AAAA responses can be set at any time.
// Install it via MockDnsResolverFactory before calling initialize().
class ProgrammableDnsResolver {
public:
  explicit ProgrammableDnsResolver(NiceMock<Network::MockDnsResolverFactory>& factory,
                                   Network::Address::IpVersion ip_version)
      : resolver_(std::make_shared<NiceMock<Network::MockDnsResolver>>()), ip_version_(ip_version) {
    EXPECT_CALL(factory, createDnsResolver(_, _, _)).WillRepeatedly(testing::Return(resolver_));

    EXPECT_CALL(*resolver_, resolveSrv(_, _))
        .WillRepeatedly(
            Invoke([this](const std::string&,
                          Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
              cb(Network::DnsResolver::ResolutionStatus::Completed, "ok",
                 std::list<Network::DnsResponse>(srv_targets_));
              return nullptr;
            }));

    EXPECT_CALL(*resolver_, resolve(_, _, _))
        .WillRepeatedly(
            Invoke([this](const std::string&, Network::DnsLookupFamily,
                          Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
              cb(Network::DnsResolver::ResolutionStatus::Completed, "ok",
                 TestUtility::makeDnsResponse(a_addresses_));
              return nullptr;
            }));
  }

  void setSrvTargets(std::list<Network::DnsResponse> targets) { srv_targets_ = std::move(targets); }

  void setAAddresses(std::list<std::string> addresses) { a_addresses_ = std::move(addresses); }

  void setAAddressesToLoopback() {
    setAAddresses({Network::Test::getLoopbackAddressString(ip_version_)});
  }

private:
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> resolver_;
  Network::Address::IpVersion ip_version_;
  std::list<Network::DnsResponse> srv_targets_;
  std::list<std::string> a_addresses_;
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

            std::string ip_value = Network::Test::getLoopbackAddressString(version_);
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

  addSrvClusterConfigModifier();
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 10000);

  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

// Verifies that changes in the SRV response add and remove cluster hosts.
//
// Sequence:
//   resolve #0 → 1 SRV target (upstream 0) → membership_total = 1
//   resolve #1 → 2 SRV targets (upstream 0 + upstream 1) → membership_total = 2
//   resolve #2+ → 1 SRV target (upstream 1 only) → membership_total = 1
TEST_P(DnsSrvClusterIntegrationTest, AddRemoveHostsViaSrvResponse) {
  setUpstreamCount(2);

  ProgrammableDnsResolver dns(dns_resolver_factory_, version_);
  dns.setAAddressesToLoopback();
  // SRV targets are set after initialize() when fake_upstreams_ ports are known.

  addSrvClusterConfigModifier();
  initialize();

  // fake_upstreams_ is populated by createUpstreams() inside initialize().
  const uint16_t port0 = fake_upstreams_[0]->localAddress()->ip()->port();
  const uint16_t port1 = fake_upstreams_[1]->localAddress()->ip()->port();

  dns.setSrvTargets({Network::DnsResponse(0, 1, port0, "svc0.local", std::chrono::seconds(60))});
  triggerDnsRefresh();
  test_server_->waitForGauge("cluster.cluster_0.membership_total", testing::Eq(1));
  EXPECT_EQ(clusterHostPorts(), (std::set<uint16_t>{port0}));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Add upstream 1 and trigger a refresh.
  dns.setSrvTargets({
      Network::DnsResponse(0, 1, port0, "svc0.local", std::chrono::seconds(60)),
      Network::DnsResponse(0, 1, port1, "svc1.local", std::chrono::seconds(60)),
  });
  triggerDnsRefresh();
  test_server_->waitForGauge("cluster.cluster_0.membership_total", testing::Eq(2));
  EXPECT_EQ(clusterHostPorts(), (std::set<uint16_t>{port0, port1}));

  // Remove upstream 0 and trigger a refresh.
  dns.setSrvTargets({Network::DnsResponse(0, 1, port1, "svc1.local", std::chrono::seconds(60))});
  triggerDnsRefresh();
  test_server_->waitForGauge("cluster.cluster_0.membership_total", testing::Eq(1));
  EXPECT_EQ(clusterHostPorts(), (std::set<uint16_t>{port1}));
}

// Verifies that changes in the A/AAAA response for an SRV target add and remove cluster hosts.
//
// The SRV record is kept constant (one target, fixed port). The A/AAAA response for that
// target changes across refreshes:
//   resolve #0 → A returns [loopback] → membership_total = 1
//   resolve #1 → A returns [loopback, extra-ip] → membership_total = 2
//   resolve #2+ → A returns [loopback] → membership_total = 1
TEST_P(DnsSrvClusterIntegrationTest, AddRemoveHostsViaChangedAaaaResponse) {
  ProgrammableDnsResolver dns(dns_resolver_factory_, version_);

  const std::string loopback = Network::Test::getLoopbackAddressString(version_);
  // An extra, non-routable address (TEST-NET per RFC 5737) exercises addition and removal.
  const std::string extra_ip =
      (version_ == Network::Address::IpVersion::v4) ? "192.0.2.1" : "100::1";
  dns.setAAddresses({loopback});
  // SRV target is set after initialize() when fake_upstreams_ port is known.

  addSrvClusterConfigModifier();
  initialize();

  // fake_upstreams_ is populated by createUpstreams() inside initialize().
  const uint16_t port = fake_upstreams_[0]->localAddress()->ip()->port();
  dns.setSrvTargets(
      {Network::DnsResponse(0, 1, port, "svc.example.com", std::chrono::seconds(60))});
  triggerDnsRefresh();
  test_server_->waitForGauge("cluster.cluster_0.membership_total", testing::Eq(1));
  EXPECT_EQ(clusterHostIps(), (std::set<std::string>{loopback}));

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Add the extra IP and trigger a refresh.
  dns.setAAddresses({loopback, extra_ip});
  triggerDnsRefresh();
  test_server_->waitForGauge("cluster.cluster_0.membership_total", testing::Eq(2));
  EXPECT_EQ(clusterHostIps(), (std::set<std::string>{loopback, extra_ip}));

  // Remove the extra IP and trigger a refresh.
  dns.setAAddresses({loopback});
  triggerDnsRefresh();
  test_server_->waitForGauge("cluster.cluster_0.membership_total", testing::Eq(1));
  EXPECT_EQ(clusterHostIps(), (std::set<std::string>{loopback}));
}

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
