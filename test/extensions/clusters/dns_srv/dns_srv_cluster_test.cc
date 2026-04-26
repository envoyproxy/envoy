#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dns_srv/v3/cluster.pb.h"

#include "source/common/config/utility.h"
#include "source/extensions/clusters/dns_srv/dns_srv_cluster.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Clusters {

class DnsSrvClusterTest : public testing::Test {

protected:
  DnsSrvClusterTest() = default;

  static constexpr char default_yaml[] = R"EOF(
    name: test_cluster
    connect_timeout: 1s
    dns_lookup_family: ALL
    cluster_type:
      name: envoy.clusters.dns_srv
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dns_srv.v3.DnsSrvClusterConfig
        srv_name: "_local_service._tcp.service.consul."
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.cares
  )EOF";

  // Creates and initializes the cluster from yaml. Caller must set up DNS mock expectations first.
  void createCluster(const std::string& yaml = default_yaml,
                     std::function<absl::Status()> init_cb = nullptr) {
    envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);

    envoy::extensions::clusters::dns_srv::v3::DnsSrvClusterConfig dns_srv_config{};
    ASSERT_TRUE(Config::Utility::translateOpaqueConfig(
                    cluster_config.cluster_type().typed_config(),
                    ProtobufMessage::getStrictValidationVisitor(), dns_srv_config)
                    .ok());

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(
        server_context_, [this]() -> Network::DnsResolverSharedPtr { return dns_resolver_; },
        nullptr, false);

    auto factory = Upstream::DnsSrvClusterFactory();
    auto status_or_cluster =
        factory.createClusterWithConfig(cluster_config, dns_srv_config, factory_context);
    THROW_IF_NOT_OK_REF(status_or_cluster.status());

    cluster_ = status_or_cluster.value().first;
    cluster_->initialize(init_cb ? init_cb : []() { return absl::OkStatus(); });
  }

  // Registers an expectation that the resolve timer will be armed (any interval).
  void expectResolveTimer() {
    resolve_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
    EXPECT_CALL(*resolve_timer_, enableTimer(_, _));
  }

  // Sets up an expectation for the top-level SRV query and captures its callback.
  void expectResolveSrv(const std::string& srv_name, Network::DnsResolver::ResolveCb& out_cb) {
    EXPECT_CALL(*dns_resolver_, resolveSrv(srv_name, _))
        .WillOnce(Invoke([&](const std::string&,
                             Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
          out_cb = cb;
          return &active_dns_query_;
        }));
  }

  // Sets up an expectation for an A/AAAA query and captures its callback.
  void expectResolve(const std::string& hostname, Network::DnsLookupFamily family,
                     Network::DnsResolver::ResolveCb& out_cb) {
    EXPECT_CALL(*dns_resolver_, resolve(hostname, family, _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                             Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
          out_cb = cb;
          return &active_dns_query_;
        }));
  }

  Network::MockActiveDnsQuery active_dns_query_;
  Event::MockTimer* resolve_timer_{};

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<Upstream::MockClusterManager> cm_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  Upstream::ClusterImplBaseSharedPtr cluster_;
};

// Verifies that after a successful SRV + A/AAAA resolution the cluster contains one host,
// and that the host's priority, weight, and port match the SRV record.
TEST_F(DnsSrvClusterTest, CreateClusterWithMinimalConfig) {
  constexpr uint32_t srv_priority = 0;
  constexpr uint32_t srv_weight = 1;
  constexpr uint32_t srv_port = 9000;
  constexpr std::chrono::seconds srv_ttl{42};

  expectResolveTimer();

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  Network::DnsResolver::ResolveCb a_callback;
  expectResolve("svc.example.com", Network::DnsLookupFamily::All, a_callback);

  createCluster();

  // Fire SRV response: one record → triggers A/AAAA resolve.
  ASSERT_TRUE(srv_callback != nullptr);
  srv_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               {{srv_priority, srv_weight, srv_port, "svc.example.com", srv_ttl}});

  // Fire A/AAAA response → triggers allTargetsResolved, host created.
  ASSERT_TRUE(a_callback != nullptr);
  a_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
             TestUtility::makeDnsResponse({"1.2.3.4"}, srv_ttl));

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->priority(), srv_priority);
  EXPECT_EQ(hosts[0]->weight(), srv_weight);
  EXPECT_EQ(hosts[0]->address()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(hosts[0]->address()->ip()->port(), srv_port);
}

// Verifies that two SRV records pointing to the same hostname but different ports each result
// in a separate host entry, even though they share the same resolved IP address.
TEST_F(DnsSrvClusterTest, SameIpTwice) {
  constexpr uint32_t srv_priority = 0;
  constexpr uint32_t srv_weight = 1;
  constexpr uint32_t srv_port1 = 9000;
  constexpr uint32_t srv_port2 = 9001;
  constexpr std::chrono::seconds srv_ttl{42};

  expectResolveTimer();

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  // Two SRV targets with the same hostname → two independent A/AAAA queries.
  std::vector<Network::DnsResolver::ResolveCb> a_callbacks;
  EXPECT_CALL(*dns_resolver_, resolve("svc.example.com", Network::DnsLookupFamily::All, _))
      .Times(2)
      .WillRepeatedly(Invoke([&](const std::string&, Network::DnsLookupFamily,
                                 Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        a_callbacks.push_back(cb);
        return &active_dns_query_;
      }));

  createCluster();

  ASSERT_TRUE(srv_callback != nullptr);
  srv_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               {
                   {srv_priority, srv_weight, srv_port1, "svc.example.com", srv_ttl},
                   {srv_priority, srv_weight, srv_port2, "svc.example.com", srv_ttl},
               });

  ASSERT_EQ(a_callbacks.size(), 2);
  for (auto& cb : a_callbacks) {
    cb(Network::DnsResolver::ResolutionStatus::Completed, "",
       TestUtility::makeDnsResponse({"1.2.3.4"}, srv_ttl));
  }

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 2);

  EXPECT_EQ(hosts[0]->priority(), srv_priority);
  EXPECT_EQ(hosts[0]->weight(), srv_weight);
  EXPECT_EQ(hosts[0]->address()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(hosts[0]->address()->ip()->port(), srv_port1);

  EXPECT_EQ(hosts[1]->priority(), srv_priority);
  EXPECT_EQ(hosts[1]->weight(), srv_weight);
  EXPECT_EQ(hosts[1]->address()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(hosts[1]->address()->ip()->port(), srv_port2);
}

// Verifies that an SRV response with two different hostnames results in two hosts,
// each resolved independently via A/AAAA queries.
TEST_F(DnsSrvClusterTest, TwoDifferentHostnames) {
  constexpr uint32_t srv_priority = 0;
  constexpr uint32_t srv_weight = 1;
  constexpr uint32_t srv_port1 = 9000;
  constexpr uint32_t srv_port2 = 9001;
  constexpr std::chrono::seconds srv_ttl{42};

  expectResolveTimer();

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  Network::DnsResolver::ResolveCb a_callback1;
  expectResolve("svc1.example.com", Network::DnsLookupFamily::All, a_callback1);

  Network::DnsResolver::ResolveCb a_callback2;
  expectResolve("svc2.example.com", Network::DnsLookupFamily::All, a_callback2);

  createCluster();

  ASSERT_TRUE(srv_callback != nullptr);
  srv_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               {
                   {srv_priority, srv_weight, srv_port1, "svc1.example.com", srv_ttl},
                   {srv_priority, srv_weight, srv_port2, "svc2.example.com", srv_ttl},
               });

  ASSERT_TRUE(a_callback1 != nullptr);
  a_callback1(Network::DnsResolver::ResolutionStatus::Completed, "",
              TestUtility::makeDnsResponse({"1.2.3.4"}, srv_ttl));

  ASSERT_TRUE(a_callback2 != nullptr);
  a_callback2(Network::DnsResolver::ResolutionStatus::Completed, "",
              TestUtility::makeDnsResponse({"5.6.7.8"}, srv_ttl));

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 2);

  EXPECT_EQ(hosts[0]->priority(), srv_priority);
  EXPECT_EQ(hosts[0]->weight(), srv_weight);
  EXPECT_EQ(hosts[0]->address()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(hosts[0]->address()->ip()->port(), srv_port1);

  EXPECT_EQ(hosts[1]->priority(), srv_priority);
  EXPECT_EQ(hosts[1]->weight(), srv_weight);
  EXPECT_EQ(hosts[1]->address()->ip()->addressAsString(), "5.6.7.8");
  EXPECT_EQ(hosts[1]->address()->ip()->port(), srv_port2);
}

// Verifies that an SRV response with two different hostnames results in two hosts,
// each resolved independently via A/AAAA queries.
TEST_F(DnsSrvClusterTest, TwoDifferentHostnamesOneFails) {
  constexpr uint32_t srv_priority = 0;
  constexpr uint32_t srv_weight = 1;
  constexpr uint32_t srv_port = 9000;
  constexpr std::chrono::seconds srv_ttl{42};

  expectResolveTimer();

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  Network::DnsResolver::ResolveCb a_callback1;
  expectResolve("ok.example.com", Network::DnsLookupFamily::All, a_callback1);

  Network::DnsResolver::ResolveCb a_callback2;
  expectResolve("fail.example.com", Network::DnsLookupFamily::All, a_callback2);

  createCluster();

  ASSERT_TRUE(srv_callback != nullptr);
  srv_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               {
                   {srv_priority, srv_weight, srv_port, "ok.example.com", srv_ttl},
                   {srv_priority, srv_weight, srv_port, "fail.example.com", srv_ttl},
               });

  ASSERT_TRUE(a_callback1 != nullptr);
  a_callback1(Network::DnsResolver::ResolutionStatus::Completed, "",
              TestUtility::makeDnsResponse({"1.2.3.4"}, srv_ttl));

  ASSERT_TRUE(a_callback2 != nullptr);
  a_callback2(Network::DnsResolver::ResolutionStatus::Failure, "dns timeout",
              TestUtility::makeDnsResponse({}, srv_ttl));

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 1);

  EXPECT_EQ(hosts[0]->priority(), srv_priority);
  EXPECT_EQ(hosts[0]->weight(), srv_weight);
  EXPECT_EQ(hosts[0]->address()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(hosts[0]->address()->ip()->port(), srv_port);
}

TEST_F(DnsSrvClusterTest, OneHostnameOneIp) {
  constexpr uint32_t srv_priority = 0;
  constexpr uint32_t srv_weight = 1;
  constexpr uint32_t srv_port = 9000;
  constexpr std::chrono::seconds srv_ttl{42};

  expectResolveTimer();

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  Network::DnsResolver::ResolveCb a_callback1;
  expectResolve("ok.example.com", Network::DnsLookupFamily::All, a_callback1);

  // Network::DnsResolver::ResolveCb a_callback2;
  // expectResolve("fail.example.com", Network::DnsLookupFamily::All, a_callback2);

  createCluster();

  ASSERT_TRUE(srv_callback != nullptr);
  srv_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               {
                   {srv_priority, srv_weight, srv_port, "ok.example.com", srv_ttl},
                   {srv_priority, srv_weight, srv_port, "5.6.7.8", srv_ttl},
               });

  ASSERT_TRUE(a_callback1 != nullptr);
  a_callback1(Network::DnsResolver::ResolutionStatus::Completed, "",
              TestUtility::makeDnsResponse({"1.2.3.4"}, srv_ttl));

  // ASSERT_TRUE(a_callback2 != nullptr);
  // a_callback2(Network::DnsResolver::ResolutionStatus::Failure, "dns timeout",
  // TestUtility::makeDnsResponse({}, srv_ttl));

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 2);

  EXPECT_EQ(hosts[0]->priority(), srv_priority);
  EXPECT_EQ(hosts[0]->weight(), srv_weight);
  // this host adds before DNS is called, becuase it's already an IP. Hence, it appears first in the
  // list.
  EXPECT_EQ(hosts[0]->address()->ip()->addressAsString(), "5.6.7.8");
  EXPECT_EQ(hosts[0]->address()->ip()->port(), srv_port);

  EXPECT_EQ(hosts[1]->priority(), srv_priority);
  EXPECT_EQ(hosts[1]->weight(), srv_weight);
  EXPECT_EQ(hosts[1]->address()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(hosts[1]->address()->ip()->port(), srv_port);
}

TEST_F(DnsSrvClusterTest, TwoIps) {
  constexpr uint32_t srv_priority = 0;
  constexpr uint32_t srv_weight = 1;
  constexpr uint32_t srv_port = 9000;
  constexpr std::chrono::seconds srv_ttl{42};

  expectResolveTimer();

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  createCluster();

  ASSERT_TRUE(srv_callback != nullptr);
  srv_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               {
                   {srv_priority, srv_weight, srv_port, "1.2.3.4", srv_ttl},
                   {srv_priority, srv_weight, srv_port, "5.6.7.8", srv_ttl},
               });

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 2);

  EXPECT_EQ(hosts[0]->priority(), srv_priority);
  EXPECT_EQ(hosts[0]->weight(), srv_weight);
  EXPECT_EQ(hosts[0]->address()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(hosts[0]->address()->ip()->port(), srv_port);

  EXPECT_EQ(hosts[1]->priority(), srv_priority);
  EXPECT_EQ(hosts[1]->weight(), srv_weight);
  EXPECT_EQ(hosts[1]->address()->ip()->addressAsString(), "5.6.7.8");
  EXPECT_EQ(hosts[1]->address()->ip()->port(), srv_port);
}

TEST_F(DnsSrvClusterTest, DontWaitForDNSOnInit) {
  static constexpr char yaml[] = R"EOF(
    name: test_cluster
    connect_timeout: 1s
    dns_lookup_family: ALL
    wait_for_warm_on_init: false
    cluster_type:
      name: envoy.clusters.dns_srv
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dns_srv.v3.DnsSrvClusterConfig
        srv_name: "_local_service._tcp.service.consul."
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.cares
  )EOF";

  expectResolveTimer();

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  // With wait_for_warm_on_init=false the init callback must fire during createCluster(),
  // before any DNS response arrives.
  bool initialized = false;
  createCluster(yaml, [&]() {
    initialized = true;
    return absl::OkStatus();
  });
  EXPECT_TRUE(initialized);

  // DNS resolution completes after init — cluster still processes it normally.
  Network::DnsResolver::ResolveCb a_callback;
  expectResolve("svc.example.com", Network::DnsLookupFamily::All, a_callback);

  ASSERT_TRUE(srv_callback != nullptr);
  srv_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               {{0, 1, 9000, "svc.example.com", std::chrono::seconds(42)}});

  ASSERT_TRUE(a_callback != nullptr);
  a_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
             TestUtility::makeDnsResponse({"1.2.3.4"}, std::chrono::seconds(42)));

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->address()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(hosts[0]->address()->ip()->port(), 9000);
}

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
