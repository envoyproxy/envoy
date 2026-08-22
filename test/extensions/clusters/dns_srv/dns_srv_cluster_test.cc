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
  void expectResolveTimer(int times = 1) {
    resolve_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
    EXPECT_CALL(*resolve_timer_, enableTimer(_, _)).Times(times);
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
  constexpr std::chrono::seconds srv_ttl{40};

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
  constexpr std::chrono::seconds srv_ttl{40};

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
  constexpr std::chrono::seconds srv_ttl{40};

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
  constexpr std::chrono::seconds srv_ttl{40};

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
  constexpr std::chrono::seconds srv_ttl{40};

  expectResolveTimer();

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  Network::DnsResolver::ResolveCb a_callback1;
  expectResolve("ok.example.com", Network::DnsLookupFamily::All, a_callback1);

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

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 2);

  EXPECT_EQ(hosts[0]->priority(), srv_priority);
  EXPECT_EQ(hosts[0]->weight(), srv_weight);
  // this host adds before DNS is called, becuase it's already an IP.
  // Hence, it appears first in the list.
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
  constexpr std::chrono::seconds srv_ttl{40};

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

// Verifies that when respect_dns_ttl is enabled the refresh timer is armed with the TTL
// returned in the A/AAAA response rather than the static dns_refresh_rate.
TEST_F(DnsSrvClusterTest, RespectDnsTtl) {
  static constexpr char yaml[] = R"EOF(
    name: test_cluster
    connect_timeout: 1s
    dns_lookup_family: ALL
    respect_dns_ttl: true
    cluster_type:
      name: envoy.clusters.dns_srv
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dns_srv.v3.DnsSrvClusterConfig
        srv_name: "_local_service._tcp.service.consul."
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.cares
  )EOF";

  constexpr std::chrono::seconds srv_ttl{40};

  resolve_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
  EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(40000), _));

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  Network::DnsResolver::ResolveCb a_callback;
  expectResolve("svc.example.com", Network::DnsLookupFamily::All, a_callback);

  createCluster(yaml);

  srv_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               {{0, 1, 9000, "svc.example.com", srv_ttl}});

  a_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
             TestUtility::makeDnsResponse({"1.2.3.4"}, srv_ttl));

  const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
  ASSERT_EQ(hosts.size(), 1);
  EXPECT_EQ(hosts[0]->address()->ip()->addressAsString(), "1.2.3.4");
  EXPECT_EQ(hosts[0]->address()->ip()->port(), 9000);
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

// Verifies that destroying the cluster while A/AAAA queries are in-flight cancels them.
// The SRV callback fires (clearing DnsSrvCluster::active_dns_query_) and starts A/AAAA
// resolution via ResolveTarget. Destroying the cluster before the A/AAAA callback arrives
// must trigger ResolveTarget::~ResolveTarget to cancel the outstanding query.
TEST_F(DnsSrvClusterTest, DestroyDuringPendingAaaaQuery) {
  resolve_timer_ = new Event::MockTimer(&server_context_.dispatcher_);

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  Network::DnsResolver::ResolveCb a_callback;
  expectResolve("svc.example.com", Network::DnsLookupFamily::All, a_callback);

  createCluster();

  // Fire SRV response — starts A/AAAA query, clears DnsSrvCluster::active_dns_query_.
  srv_callback(Network::DnsResolver::ResolutionStatus::Completed, "",
               {{0, 1, 9000, "svc.example.com", std::chrono::seconds(42)}});

  // A/AAAA query still pending. Destroying the cluster must cancel it via ResolveTarget dtor.
  EXPECT_CALL(active_dns_query_, cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
  cluster_.reset();
}

// Verifies that destroying the cluster while an SRV query is in-flight cancels the query.
TEST_F(DnsSrvClusterTest, DestroyDuringPendingSrvQuery) {
  resolve_timer_ = new Event::MockTimer(&server_context_.dispatcher_);

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  createCluster();

  // The SRV query is still pending. Destroying the cluster must cancel it.
  EXPECT_CALL(active_dns_query_, cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
  cluster_.reset();
}

// Verifies that when the top-level SRV query fails, update_failure_ is incremented,
// the refresh timer is rescheduled, and the host set remains empty.
TEST_F(DnsSrvClusterTest, SrvResolutionFailureIncrementsStats) {
  expectResolveTimer();

  Network::DnsResolver::ResolveCb srv_callback;
  expectResolveSrv("_local_service._tcp.service.consul.", srv_callback);

  createCluster();

  ASSERT_TRUE(srv_callback != nullptr);
  srv_callback(Network::DnsResolver::ResolutionStatus::Failure, "dns timeout", {});

  EXPECT_EQ(1, cluster_->info()->configUpdateStats().update_attempt_.value());
  EXPECT_EQ(1, cluster_->info()->configUpdateStats().update_failure_.value());
  EXPECT_TRUE(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().empty());
}

// Verifies that specifying a DNS resolver other than c-ares causes InvalidArgumentError,
// because only c-ares implements SRV resolution at the moment.
TEST_F(DnsSrvClusterTest, NonCaresResolverReturnsInvalidArgumentError) {
  static constexpr char yaml[] = R"EOF(
    name: test_cluster
    connect_timeout: 1s
    dns_lookup_family: ALL
    cluster_type:
      name: envoy.clusters.dns_srv
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dns_srv.v3.DnsSrvClusterConfig
        srv_name: "_local_service._tcp.service.consul."
    typed_dns_resolver_config:
      name: envoy.network.dns_resolver.getaddrinfo
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);

  Envoy::Upstream::ClusterFactoryContextImpl factory_context(
      server_context_, [this]() -> Network::DnsResolverSharedPtr { return dns_resolver_; }, nullptr,
      false);

  auto factory = Upstream::DnsSrvClusterFactory();
  auto status_or_cluster = factory.create(cluster_config, factory_context);

  ASSERT_FALSE(status_or_cluster.ok());
  EXPECT_EQ(status_or_cluster.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status_or_cluster.status().message(),
              testing::HasSubstr("envoy.network.dns_resolver.getaddrinfo"));
}

// Verifies that when the SRV response changes on a subsequent refresh the cluster correctly
// adds newly-appearing hosts and removes hosts that are no longer present.
TEST_F(DnsSrvClusterTest, AddRemoveHostsViaSrvResponse) {
  constexpr uint32_t srv_priority = 0;
  constexpr uint32_t srv_weight = 1;
  constexpr uint32_t port1 = 9000;
  constexpr uint32_t port2 = 9001;
  constexpr uint32_t port3 = 9002;
  constexpr std::chrono::seconds srv_ttl{40};

  // The timer fires twice: once after the first full resolve, once after the second.
  expectResolveTimer(2);

  // Both SRV rounds share the same query name; capture each callback in sequence.
  Network::DnsResolver::ResolveCb srv_callback1;
  Network::DnsResolver::ResolveCb srv_callback2;
  EXPECT_CALL(*dns_resolver_, resolveSrv("_local_service._tcp.service.consul.", _))
      .WillOnce(Invoke(
          [&](const std::string&, Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
            srv_callback1 = cb;
            return &active_dns_query_;
          }))
      .WillOnce(Invoke(
          [&](const std::string&, Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
            srv_callback2 = cb;
            return &active_dns_query_;
          }));

  // svc1 is present in both rounds → its A/AAAA callback is captured twice.
  Network::DnsResolver::ResolveCb a_svc1_r1;
  Network::DnsResolver::ResolveCb a_svc1_r2;
  EXPECT_CALL(*dns_resolver_, resolve("svc1.example.com", Network::DnsLookupFamily::All, _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        a_svc1_r1 = cb;
        return &active_dns_query_;
      }))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        a_svc1_r2 = cb;
        return &active_dns_query_;
      }));

  // svc2 only appears in round 1.
  Network::DnsResolver::ResolveCb a_svc2_r1;
  EXPECT_CALL(*dns_resolver_, resolve("svc2.example.com", Network::DnsLookupFamily::All, _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        a_svc2_r1 = cb;
        return &active_dns_query_;
      }));

  // svc3 only appears in round 2.
  Network::DnsResolver::ResolveCb a_svc3_r2;
  EXPECT_CALL(*dns_resolver_, resolve("svc3.example.com", Network::DnsLookupFamily::All, _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        a_svc3_r2 = cb;
        return &active_dns_query_;
      }));

  createCluster();

  // ---------- Round 1 ----------
  // SRV returns svc1 (port1) and svc2 (port2).
  ASSERT_TRUE(srv_callback1 != nullptr);
  srv_callback1(Network::DnsResolver::ResolutionStatus::Completed, "",
                {
                    {srv_priority, srv_weight, port1, "svc1.example.com", srv_ttl},
                    {srv_priority, srv_weight, port2, "svc2.example.com", srv_ttl},
                });

  ASSERT_TRUE(a_svc1_r1 != nullptr);
  a_svc1_r1(Network::DnsResolver::ResolutionStatus::Completed, "",
            TestUtility::makeDnsResponse({"1.2.3.4"}, srv_ttl));

  ASSERT_TRUE(a_svc2_r1 != nullptr);
  a_svc2_r1(Network::DnsResolver::ResolutionStatus::Completed, "",
            TestUtility::makeDnsResponse({"5.6.7.8"}, srv_ttl));

  {
    const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    ASSERT_EQ(hosts.size(), 2);
    absl::flat_hash_set<std::string> addresses;
    for (const auto& h : hosts) {
      addresses.insert(h->address()->asString());
    }
    EXPECT_TRUE(addresses.count("1.2.3.4:" + std::to_string(port1)));
    EXPECT_TRUE(addresses.count("5.6.7.8:" + std::to_string(port2)));
  }

  // ---------- Round 2 ----------
  // Trigger the refresh timer → starts the second SRV resolution.
  resolve_timer_->invokeCallback();

  // SRV now returns svc1 (kept, same port) and svc3 (new); svc2 is gone.
  ASSERT_TRUE(srv_callback2 != nullptr);
  srv_callback2(Network::DnsResolver::ResolutionStatus::Completed, "",
                {
                    {srv_priority, srv_weight, port1, "svc1.example.com", srv_ttl},
                    {srv_priority, srv_weight, port3, "svc3.example.com", srv_ttl},
                });

  ASSERT_TRUE(a_svc1_r2 != nullptr);
  a_svc1_r2(Network::DnsResolver::ResolutionStatus::Completed, "",
            TestUtility::makeDnsResponse({"1.2.3.4"}, srv_ttl));

  ASSERT_TRUE(a_svc3_r2 != nullptr);
  a_svc3_r2(Network::DnsResolver::ResolutionStatus::Completed, "",
            TestUtility::makeDnsResponse({"9.10.11.12"}, srv_ttl));

  {
    const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    ASSERT_EQ(hosts.size(), 2);
    absl::flat_hash_set<std::string> addresses;
    for (const auto& h : hosts) {
      addresses.insert(h->address()->asString());
    }
    // svc1 (1.2.3.4:port1) kept; svc3 (9.10.11.12:port3) added; svc2 (5.6.7.8:port2) removed.
    EXPECT_TRUE(addresses.count("1.2.3.4:" + std::to_string(port1)));
    EXPECT_TRUE(addresses.count("9.10.11.12:" + std::to_string(port3)));
    EXPECT_FALSE(addresses.count("5.6.7.8:" + std::to_string(port2)));
  }
}

// Verifies that when the A/AAAA response for an SRV target changes on a subsequent refresh
// the cluster correctly adds newly-appearing addresses and removes stale ones.
TEST_F(DnsSrvClusterTest, AddRemoveHostsViaChangedAaaaResponse) {
  constexpr uint32_t srv_priority = 0;
  constexpr uint32_t srv_weight = 1;
  constexpr uint32_t srv_port = 9000;
  constexpr std::chrono::seconds srv_ttl{40};

  expectResolveTimer(2);

  // Both rounds issue the same SRV query; capture both callbacks.
  Network::DnsResolver::ResolveCb srv_callback1;
  Network::DnsResolver::ResolveCb srv_callback2;
  EXPECT_CALL(*dns_resolver_, resolveSrv("_local_service._tcp.service.consul.", _))
      .WillOnce(Invoke(
          [&](const std::string&, Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
            srv_callback1 = cb;
            return &active_dns_query_;
          }))
      .WillOnce(Invoke(
          [&](const std::string&, Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
            srv_callback2 = cb;
            return &active_dns_query_;
          }));

  // The same hostname is re-resolved each round with different IP sets.
  Network::DnsResolver::ResolveCb a_callback_r1;
  Network::DnsResolver::ResolveCb a_callback_r2;
  EXPECT_CALL(*dns_resolver_, resolve("svc.example.com", Network::DnsLookupFamily::All, _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        a_callback_r1 = cb;
        return &active_dns_query_;
      }))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        a_callback_r2 = cb;
        return &active_dns_query_;
      }));

  createCluster();

  // ---------- Round 1 ----------
  // SRV returns a single target; A/AAAA resolves to two addresses.
  ASSERT_TRUE(srv_callback1 != nullptr);
  srv_callback1(Network::DnsResolver::ResolutionStatus::Completed, "",
                {{srv_priority, srv_weight, srv_port, "svc.example.com", srv_ttl}});

  ASSERT_TRUE(a_callback_r1 != nullptr);
  a_callback_r1(Network::DnsResolver::ResolutionStatus::Completed, "",
                TestUtility::makeDnsResponse({"1.2.3.4", "5.6.7.8"}, srv_ttl));

  {
    const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    ASSERT_EQ(hosts.size(), 2);
    absl::flat_hash_set<std::string> ips;
    for (const auto& h : hosts) {
      ips.insert(h->address()->ip()->addressAsString());
    }
    EXPECT_TRUE(ips.count("1.2.3.4"));
    EXPECT_TRUE(ips.count("5.6.7.8"));
  }

  // ---------- Round 2 ----------
  // Trigger the refresh timer → starts the second SRV resolution.
  resolve_timer_->invokeCallback();

  // Same SRV response; A/AAAA now returns 5.6.7.8 and 9.10.11.12 (1.2.3.4 is gone).
  ASSERT_TRUE(srv_callback2 != nullptr);
  srv_callback2(Network::DnsResolver::ResolutionStatus::Completed, "",
                {{srv_priority, srv_weight, srv_port, "svc.example.com", srv_ttl}});

  ASSERT_TRUE(a_callback_r2 != nullptr);
  a_callback_r2(Network::DnsResolver::ResolutionStatus::Completed, "",
                TestUtility::makeDnsResponse({"5.6.7.8", "9.10.11.12"}, srv_ttl));

  {
    const auto& hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();
    ASSERT_EQ(hosts.size(), 2);
    absl::flat_hash_set<std::string> ips;
    for (const auto& h : hosts) {
      ips.insert(h->address()->ip()->addressAsString());
    }
    EXPECT_FALSE(ips.count("1.2.3.4"));   // removed
    EXPECT_TRUE(ips.count("5.6.7.8"));    // kept
    EXPECT_TRUE(ips.count("9.10.11.12")); // added
    for (const auto& h : hosts) {
      EXPECT_EQ(h->address()->ip()->port(), srv_port);
    }
  }
}

} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
