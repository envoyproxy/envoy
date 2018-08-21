#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/http/codec.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/config/metadata.h"
#include "common/json/config_schemas.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ContainerEq;
using testing::Invoke;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {
namespace {

std::list<std::string> hostListToAddresses(const HostVector& hosts) {
  std::list<std::string> addresses;
  for (const HostSharedPtr& host : hosts) {
    addresses.push_back(host->address()->asString());
  }

  return addresses;
}

std::shared_ptr<const HostVector>
makeHostsFromHostsPerLocality(HostsPerLocalityConstSharedPtr hosts_per_locality) {
  HostVector hosts;

  for (const auto& locality_hosts : hosts_per_locality->get()) {
    for (const auto& host : locality_hosts) {
      hosts.emplace_back(host);
    }
  }

  return std::make_shared<const HostVector>(hosts);
}

struct ResolverData {
  ResolverData(Network::MockDnsResolver& dns_resolver, Event::MockDispatcher& dispatcher) {
    timer_ = new Event::MockTimer(&dispatcher);
    expectResolve(dns_resolver);
  }

  void expectResolve(Network::MockDnsResolver& dns_resolver) {
    EXPECT_CALL(dns_resolver, resolve(_, _, _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                             Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
          dns_callback_ = cb;
          return &active_dns_query_;
        }))
        .RetiresOnSaturation();
  }

  Event::MockTimer* timer_;
  Network::DnsResolver::ResolveCb dns_callback_;
  Network::MockActiveDnsQuery active_dns_query_;
};

typedef std::tuple<std::string, Network::DnsLookupFamily, std::list<std::string>>
    StrictDnsConfigTuple;
std::vector<StrictDnsConfigTuple> generateStrictDnsParams() {
  std::vector<StrictDnsConfigTuple> dns_config;
  {
    std::string family_json("");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V4Only);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  {
    std::string family_json(R"EOF("dns_lookup_family": "v4_only",)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V4Only);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  {
    std::string family_json(R"EOF("dns_lookup_family": "v6_only",)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V6Only);
    std::list<std::string> dns_response{"::1", "::2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  {
    std::string family_json(R"EOF("dns_lookup_family": "auto",)EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::Auto);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  return dns_config;
}

class StrictDnsParamTest : public testing::TestWithParam<StrictDnsConfigTuple> {};

INSTANTIATE_TEST_CASE_P(DnsParam, StrictDnsParamTest, testing::ValuesIn(generateStrictDnsParams()));

TEST_P(StrictDnsParamTest, ImmediateResolve) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  auto dns_resolver = std::make_shared<NiceMock<Network::MockDnsResolver>>();
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  ReadyWatcher initialized;
  NiceMock<Runtime::MockRandomGenerator> random;
  const std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "strict_dns",
  )EOF" + std::get<0>(GetParam()) +
                           R"EOF(
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://foo.bar.com:443"}]
  }
  )EOF";
  EXPECT_CALL(initialized, ready());
  EXPECT_CALL(*dns_resolver, resolve("foo.bar.com", std::get<1>(GetParam()), _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        cb(TestUtility::makeDnsResponse(std::get<2>(GetParam())));
        return nullptr;
      }));
  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromJson(json);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);

  StrictDnsClusterImpl cluster(cluster_config, runtime, dns_resolver, factory_context,
                               std::move(scope), false);
  cluster.initialize([&]() -> void { initialized.ready(); });
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

// Resolve zero hosts, while using health checking.
TEST(StrictDnsClusterImplTest, ZeroHostsHealthChecker) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  auto dns_resolver = std::make_shared<Network::MockDnsResolver>();
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<MockClusterManager> cm;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;
  ReadyWatcher initialized;

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    hosts: [{ socket_address: { address: foo.bar.com, port_value: 443 }}]
  )EOF";

  ResolverData resolver(*dns_resolver, dispatcher);
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StrictDnsClusterImpl cluster(cluster_config, runtime, dns_resolver, factory_context,
                               std::move(scope), false);
  std::shared_ptr<MockHealthChecker> health_checker(new MockHealthChecker());
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  cluster.setHealthChecker(health_checker);
  cluster.initialize([&]() -> void { initialized.ready(); });

  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(initialized, ready());
  EXPECT_CALL(*resolver.timer_, enableTimer(_));
  resolver.dns_callback_({});
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

TEST(StrictDnsClusterImplTest, Basic) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  auto dns_resolver = std::make_shared<NiceMock<Network::MockDnsResolver>>();
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;
  // gmock matches in LIFO order which is why these are swapped.
  ResolverData resolver2(*dns_resolver, dispatcher);
  ResolverData resolver1(*dns_resolver, dispatcher);

  const std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "strict_dns",
    "dns_refresh_rate_ms": 4000,
    "lb_type": "round_robin",
    "circuit_breakers": {
      "default": {
        "max_connections": 43,
        "max_pending_requests": 57,
        "max_requests": 50,
        "max_retries": 10
      },
      "high": {
        "max_connections": 1,
        "max_pending_requests": 2,
        "max_requests": 3,
        "max_retries": 4
      }
    },
    "max_requests_per_connection": 3,
    "http2_settings": {
       "hpack_table_size": 0
     },
    "hosts": [{"url": "tcp://localhost1:11001"},
              {"url": "tcp://localhost2:11002"}]
  }
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromJson(json);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StrictDnsClusterImpl cluster(cluster_config, runtime, dns_resolver, factory_context,
                               std::move(scope), false);
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.default.max_connections", 43));
  EXPECT_EQ(43U, cluster.info()->resourceManager(ResourcePriority::Default).connections().max());
  EXPECT_CALL(runtime.snapshot_,
              getInteger("circuit_breakers.name.default.max_pending_requests", 57));
  EXPECT_EQ(57U,
            cluster.info()->resourceManager(ResourcePriority::Default).pendingRequests().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.default.max_requests", 50));
  EXPECT_EQ(50U, cluster.info()->resourceManager(ResourcePriority::Default).requests().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.default.max_retries", 10));
  EXPECT_EQ(10U, cluster.info()->resourceManager(ResourcePriority::Default).retries().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.high.max_connections", 1));
  EXPECT_EQ(1U, cluster.info()->resourceManager(ResourcePriority::High).connections().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.high.max_pending_requests", 2));
  EXPECT_EQ(2U, cluster.info()->resourceManager(ResourcePriority::High).pendingRequests().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.high.max_requests", 3));
  EXPECT_EQ(3U, cluster.info()->resourceManager(ResourcePriority::High).requests().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.high.max_retries", 4));
  EXPECT_EQ(4U, cluster.info()->resourceManager(ResourcePriority::High).retries().max());
  EXPECT_EQ(3U, cluster.info()->maxRequestsPerConnection());
  EXPECT_EQ(0U, cluster.info()->http2Settings().hpack_table_size_);

  cluster.info()->stats().upstream_rq_total_.inc();
  EXPECT_EQ(1UL, stats.counter("cluster.name.upstream_rq_total").value());

  EXPECT_CALL(runtime.snapshot_, featureEnabled("upstream.maintenance_mode.name", 0));
  EXPECT_FALSE(cluster.info()->maintenanceMode());

  ReadyWatcher membership_updated;
  cluster.prioritySet().addMemberUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) -> void { membership_updated.ready(); });

  cluster.initialize([] {});

  resolver1.expectResolve(*dns_resolver);
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->hostname());

  resolver1.expectResolve(*dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.expectResolve(*dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.3"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  // Make sure we de-dup the same address.
  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver2.dns_callback_(TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001", "10.0.0.1:11002"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(1UL,
            cluster.prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  for (const HostSharedPtr& host : cluster.prioritySet().hostSetsPerPriority()[0]->hosts()) {
    EXPECT_EQ(cluster.info().get(), &host->cluster());
  }

  // Make sure we cancel.
  resolver1.expectResolve(*dns_resolver);
  resolver1.timer_->callback_();
  resolver2.expectResolve(*dns_resolver);
  resolver2.timer_->callback_();

  EXPECT_CALL(resolver1.active_dns_query_, cancel());
  EXPECT_CALL(resolver2.active_dns_query_, cancel());
}

// Verifies that host removal works correctly when hosts are being health checked
// but the cluster is configured to always remove hosts
TEST(StrictDnsClusterImplTest, HostRemovalActiveHealthSkipped) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  auto dns_resolver = std::make_shared<Network::MockDnsResolver>();
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<MockClusterManager> cm;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    drain_connections_on_host_removal: true
    hosts: [{ socket_address: { address: foo.bar.com, port_value: 443 }}]
  )EOF";

  ResolverData resolver(*dns_resolver, dispatcher);
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StrictDnsClusterImpl cluster(cluster_config, runtime, dns_resolver, factory_context,
                               std::move(scope), false);
  std::shared_ptr<MockHealthChecker> health_checker(new MockHealthChecker());
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  cluster.setHealthChecker(health_checker);
  cluster.initialize([&]() -> void {});

  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(*resolver.timer_, enableTimer(_)).Times(2);
  resolver.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // Verify that both endpoints are initially marked with FAILED_ACTIVE_HC, then
  // clear the flag to simulate that these endpoints have been sucessfully health
  // checked.
  {
    const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(2UL, hosts.size());

    for (size_t i = 0; i < hosts.size(); ++i) {
      EXPECT_TRUE(hosts[i]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
      hosts[i]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    }
  }

  // Re-resolve the DNS name with only one record
  resolver.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1"}));

  const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(1UL, hosts.size());
}

TEST(StrictDnsClusterImplTest, LoadAssignmentBasic) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  auto dns_resolver = std::make_shared<NiceMock<Network::MockDnsResolver>>();
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;
  // gmock matches in LIFO order which is why these are swapped.
  ResolverData resolver2(*dns_resolver, dispatcher);
  ResolverData resolver1(*dns_resolver, dispatcher);

  const std::string yaml = R"EOF(
    name: name
    type: STRICT_DNS

    dns_lookup_family: V4_ONLY
    connect_timeout: 0.25s
    dns_refresh_rate: 4s

    lb_policy: ROUND_ROBIN

    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connections: 43
        max_pending_requests: 57
        max_requests: 50
        max_retries: 10
      - priority: HIGH
        max_connections: 1
        max_pending_requests: 2
        max_requests: 3
        max_retries: 4

    max_requests_per_connection: 3

    http2_protocol_options:
      hpack_table_size: 0

    load_assignment:
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: localhost1
                port_value: 11001
            health_check_config:
              port_value: 8000
        - endpoint:
            address:
              socket_address:
                address: localhost2
                port_value: 11002
            health_check_config:
              port_value: 8000
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StrictDnsClusterImpl cluster(cluster_config, runtime, dns_resolver, factory_context,
                               std::move(scope), false);

  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.default.max_connections", 43));
  EXPECT_EQ(43U, cluster.info()->resourceManager(ResourcePriority::Default).connections().max());
  EXPECT_CALL(runtime.snapshot_,
              getInteger("circuit_breakers.name.default.max_pending_requests", 57));
  EXPECT_EQ(57U,
            cluster.info()->resourceManager(ResourcePriority::Default).pendingRequests().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.default.max_requests", 50));
  EXPECT_EQ(50U, cluster.info()->resourceManager(ResourcePriority::Default).requests().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.default.max_retries", 10));
  EXPECT_EQ(10U, cluster.info()->resourceManager(ResourcePriority::Default).retries().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.high.max_connections", 1));
  EXPECT_EQ(1U, cluster.info()->resourceManager(ResourcePriority::High).connections().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.high.max_pending_requests", 2));
  EXPECT_EQ(2U, cluster.info()->resourceManager(ResourcePriority::High).pendingRequests().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.high.max_requests", 3));
  EXPECT_EQ(3U, cluster.info()->resourceManager(ResourcePriority::High).requests().max());
  EXPECT_CALL(runtime.snapshot_, getInteger("circuit_breakers.name.high.max_retries", 4));
  EXPECT_EQ(4U, cluster.info()->resourceManager(ResourcePriority::High).retries().max());
  EXPECT_EQ(3U, cluster.info()->maxRequestsPerConnection());
  EXPECT_EQ(0U, cluster.info()->http2Settings().hpack_table_size_);

  cluster.info()->stats().upstream_rq_total_.inc();
  EXPECT_EQ(1UL, stats.counter("cluster.name.upstream_rq_total").value());

  EXPECT_CALL(runtime.snapshot_, featureEnabled("upstream.maintenance_mode.name", 0));
  EXPECT_FALSE(cluster.info()->maintenanceMode());

  ReadyWatcher membership_updated;
  cluster.prioritySet().addMemberUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) -> void { membership_updated.ready(); });

  cluster.initialize([] {});

  resolver1.expectResolve(*dns_resolver);
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->hostname());

  resolver1.expectResolve(*dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.expectResolve(*dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.3"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  // Make sure we de-dup the same address.
  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver2.dns_callback_(TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001", "10.0.0.1:11002"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(1UL,
            cluster.prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  for (const HostSharedPtr& host : cluster.prioritySet().hostSetsPerPriority()[0]->hosts()) {
    EXPECT_EQ(cluster.info().get(), &host->cluster());
  }

  // Make sure we cancel.
  resolver1.expectResolve(*dns_resolver);
  resolver1.timer_->callback_();
  resolver2.expectResolve(*dns_resolver);
  resolver2.timer_->callback_();

  EXPECT_CALL(resolver1.active_dns_query_, cancel());
  EXPECT_CALL(resolver2.active_dns_query_, cancel());
}

TEST(StrictDnsClusterImplTest, LoadAssignmentBasicMultiplePriorities) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  auto dns_resolver = std::make_shared<NiceMock<Network::MockDnsResolver>>();
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;
  // gmock matches in LIFO order which is why these are swapped.
  ResolverData resolver3(*dns_resolver, dispatcher);
  ResolverData resolver2(*dns_resolver, dispatcher);
  ResolverData resolver1(*dns_resolver, dispatcher);

  const std::string yaml = R"EOF(
    name: name
    type: STRICT_DNS

    dns_lookup_family: V4_ONLY
    connect_timeout: 0.25s
    dns_refresh_rate: 4s

    lb_policy: ROUND_ROBIN

    load_assignment:
      endpoints:
      - priority: 0
        lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: localhost1
                port_value: 11001
            health_check_config:
              port_value: 8000
        - endpoint:
            address:
              socket_address:
                address: localhost2
                port_value: 11002
            health_check_config:
              port_value: 8000

      - priority: 1
        lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: localhost3
                port_value: 11003
            health_check_config:
              port_value: 8000
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StrictDnsClusterImpl cluster(cluster_config, runtime, dns_resolver, factory_context,
                               std::move(scope), false);
  ReadyWatcher membership_updated;
  cluster.prioritySet().addMemberUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) -> void { membership_updated.ready(); });

  cluster.initialize([] {});

  resolver1.expectResolve(*dns_resolver);
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->hostname());

  resolver1.expectResolve(*dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.expectResolve(*dns_resolver);
  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.timer_->callback_();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(TestUtility::makeDnsResponse({"127.0.0.3"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  // Make sure we de-dup the same address.
  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver2.dns_callback_(TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001", "10.0.0.1:11002"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(1UL,
            cluster.prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  for (const HostSharedPtr& host : cluster.prioritySet().hostSetsPerPriority()[0]->hosts()) {
    EXPECT_EQ(cluster.info().get(), &host->cluster());
  }

  EXPECT_CALL(*resolver3.timer_, enableTimer(std::chrono::milliseconds(4000)));
  EXPECT_CALL(membership_updated, ready());
  resolver3.dns_callback_(TestUtility::makeDnsResponse({"192.168.1.1", "192.168.1.2"}));

  // Make sure we have multiple priorities.
  EXPECT_THAT(
      std::list<std::string>({"192.168.1.1:11003", "192.168.1.2:11003"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[1]->hosts())));

  // Make sure we cancel.
  resolver1.expectResolve(*dns_resolver);
  resolver1.timer_->callback_();
  resolver2.expectResolve(*dns_resolver);
  resolver2.timer_->callback_();
  resolver3.expectResolve(*dns_resolver);
  resolver3.timer_->callback_();

  EXPECT_CALL(resolver1.active_dns_query_, cancel());
  EXPECT_CALL(resolver2.active_dns_query_, cancel());
  EXPECT_CALL(resolver3.active_dns_query_, cancel());
}

TEST(HostImplTest, HostCluster) {
  MockCluster cluster;
  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 1);
  EXPECT_EQ(cluster.info_.get(), &host->cluster());
  EXPECT_EQ("", host->hostname());
  EXPECT_FALSE(host->canary());
  EXPECT_EQ("", host->locality().zone());
}

TEST(HostImplTest, Weight) {
  MockCluster cluster;

  EXPECT_EQ(1U, makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 0)->weight());
  EXPECT_EQ(128U, makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 128)->weight());
  EXPECT_EQ(128U, makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 129)->weight());

  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 50);
  EXPECT_EQ(50U, host->weight());
  host->weight(51);
  EXPECT_EQ(51U, host->weight());
  host->weight(0);
  EXPECT_EQ(1U, host->weight());
  host->weight(129);
  EXPECT_EQ(128U, host->weight());
}

TEST(HostImplTest, HostnameCanaryAndLocality) {
  MockCluster cluster;
  envoy::api::v2::core::Metadata metadata;
  Config::Metadata::mutableMetadataValue(metadata, Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);
  envoy::api::v2::core::Locality locality;
  locality.set_region("oceania");
  locality.set_zone("hello");
  locality.set_sub_zone("world");
  HostImpl host(cluster.info_, "lyft.com", Network::Utility::resolveUrl("tcp://10.0.0.1:1234"),
                metadata, 1, locality,
                envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance());
  EXPECT_EQ(cluster.info_.get(), &host.cluster());
  EXPECT_EQ("lyft.com", host.hostname());
  EXPECT_TRUE(host.canary());
  EXPECT_EQ("oceania", host.locality().region());
  EXPECT_EQ("hello", host.locality().zone());
  EXPECT_EQ("world", host.locality().sub_zone());
}

TEST(StaticClusterImplTest, InitialHosts) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    hosts:
    - socket_address:
        address: 10.0.0.1
        port_value: 443
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST(StaticClusterImplTest, EmptyHostname) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string json = R"EOF(
  {
    "name": "staticcluster",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "random",
    "hosts": [{"url": "tcp://10.0.0.1:11001"}]
  }
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromJson(json);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST(StaticClusterImplTest, LoadAssignmentEmptyHostname) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 10.0.0.1
                port_value: 443
            health_check_config:
              port_value: 8000
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST(StaticClusterImplTest, LoadAssignmentMultiplePriorities) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      endpoints:
      - priority: 0
        lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 10.0.0.1
                port_value: 443
            health_check_config:
              port_value: 8000
        - endpoint:
            address:
              socket_address:
                address: 10.0.0.2
                port_value: 443
            health_check_config:
              port_value: 8000

      - priority: 1
        lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 10.0.0.3
                port_value: 443
            health_check_config:
              port_value: 8000
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[1]->healthyHosts().size());
  EXPECT_EQ("", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST(StaticClusterImplTest, LoadAssignmentLocality) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      endpoints:
      - locality:
          region: oceania
          zone: hello
          sub_zone: world
        lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 10.0.0.1
                port_value: 443
            health_check_config:
              port_value: 8000
        - endpoint:
            address:
              socket_address:
                address: 10.0.0.2
                port_value: 443
            health_check_config:
              port_value: 8000
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(hosts.size(), 2);
  for (int i = 0; i < 2; ++i) {
    const auto& locality = hosts[i]->locality();
    EXPECT_EQ("oceania", locality.region());
    EXPECT_EQ("hello", locality.zone());
    EXPECT_EQ("world", locality.sub_zone());
  }
  EXPECT_EQ(nullptr, cluster.prioritySet().hostSetsPerPriority()[0]->localityWeights());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST(StaticClusterImplTest, AltStatName) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;
  NiceMock<Event::MockDispatcher> dispatcher;

  const std::string yaml = R"EOF(
    name: staticcluster
    alt_stat_name: staticcluster_stats
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    hosts: [{ socket_address: { address: 10.0.0.1, port_value: 443 }}]
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), false);
  cluster.initialize([] {});
  // Increment a stat and verify it is emitted with alt_stat_name
  cluster.info()->stats().upstream_rq_total_.inc();
  EXPECT_EQ(1UL, stats.counter("cluster.staticcluster_stats.upstream_rq_total").value());
}

TEST(StaticClusterImplTest, RingHash) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string json = R"EOF(
  {
    "name": "staticcluster",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "ring_hash",
    "hosts": [{"url": "tcp://10.0.0.1:11001"}]
  }
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromJson(json);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), true);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(LoadBalancerType::RingHash, cluster.info()->lbType());
  EXPECT_TRUE(cluster.info()->addedViaApi());
}

TEST(StaticClusterImplTest, OutlierDetector) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;
  NiceMock<Event::MockDispatcher> dispatcher;

  const std::string json = R"EOF(
  {
    "name": "addressportconfig",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "random",
    "hosts": [{"url": "tcp://10.0.0.1:11001"},
              {"url": "tcp://10.0.0.2:11002"}]
  }
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromJson(json);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), false);

  Outlier::MockDetector* detector = new Outlier::MockDetector();
  EXPECT_CALL(*detector, addChangedStateCb(_));
  cluster.setOutlierDetector(Outlier::DetectorSharedPtr{detector});
  cluster.initialize([] {});

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->stats().membership_healthy_.value());

  // Set a single host as having failed and fire outlier detector callbacks. This should result
  // in only a single healthy host.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->outlierDetector().putHttpResponseCode(
      503);
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_NE(cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts()[0],
            cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);

  // Bring the host back online.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->stats().membership_healthy_.value());
}

TEST(StaticClusterImplTest, HealthyStat) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string json = R"EOF(
  {
    "name": "addressportconfig",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "random",
    "hosts": [{"url": "tcp://10.0.0.1:11001"},
              {"url": "tcp://10.0.0.2:11002"}]
  }
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromJson(json);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), false);

  Outlier::MockDetector* outlier_detector = new NiceMock<Outlier::MockDetector>();
  cluster.setOutlierDetector(Outlier::DetectorSharedPtr{outlier_detector});

  std::shared_ptr<MockHealthChecker> health_checker(new NiceMock<MockHealthChecker>());
  cluster.setHealthChecker(health_checker);

  ReadyWatcher initialized;
  cluster.initialize([&initialized] { initialized.ready(); });

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_healthy_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                               HealthTransition::Changed);
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_CALL(initialized, ready());
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                               HealthTransition::Changed);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                               HealthTransition::Changed);
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->stats().membership_healthy_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_healthy_.value());
}

TEST(StaticClusterImplTest, UrlConfig) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string json = R"EOF(
  {
    "name": "addressportconfig",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "random",
    "hosts": [{"url": "tcp://10.0.0.1:11001"},
              {"url": "tcp://10.0.0.2:11002"}]
  }
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromJson(json);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  StaticClusterImpl cluster(cluster_config, runtime, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::Default).connections().max());
  EXPECT_EQ(1024U,
            cluster.info()->resourceManager(ResourcePriority::Default).pendingRequests().max());
  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::Default).requests().max());
  EXPECT_EQ(3U, cluster.info()->resourceManager(ResourcePriority::Default).retries().max());
  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::High).connections().max());
  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::High).pendingRequests().max());
  EXPECT_EQ(1024U, cluster.info()->resourceManager(ResourcePriority::High).requests().max());
  EXPECT_EQ(3U, cluster.info()->resourceManager(ResourcePriority::High).retries().max());
  EXPECT_EQ(0U, cluster.info()->maxRequestsPerConnection());
  EXPECT_EQ(Http::Http2Settings::DEFAULT_HPACK_TABLE_SIZE,
            cluster.info()->http2Settings().hpack_table_size_);
  EXPECT_EQ(LoadBalancerType::Random, cluster.info()->lbType());
  EXPECT_THAT(
      std::list<std::string>({"10.0.0.1:11001", "10.0.0.2:11002"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(1UL,
            cluster.prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthChecker().setUnhealthy();
}

TEST(StaticClusterImplTest, UnsupportedLBType) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<MockClusterManager> cm;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string json = R"EOF(
  {
    "name": "addressportconfig",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "fakelbtype",
    "hosts": [{"url": "tcp://192.168.1.1:22"},
              {"url": "tcp://192.168.1.2:44"}]
  }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      {
        envoy::api::v2::Cluster cluster_config = parseClusterFromJson(json);
        Envoy::Stats::ScopePtr scope =
            stats.createScope(fmt::format("cluster.{}.", cluster_config.alt_stat_name().empty()
                                                             ? cluster_config.name()
                                                             : cluster_config.alt_stat_name()));
        Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
            ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
        StaticClusterImpl(cluster_config, runtime, factory_context, std::move(scope), false);
      },
      EnvoyException,
      "JSON at lines 2-9 does not conform to schema.\n"
      " Invalid schema: #/properties/lb_type\n"
      " Schema violation: enum\n"
      " Offending document key: #/lb_type");
}

TEST(StaticClusterImplTest, MalformedHostIP) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    hosts: [{ socket_address: { address: foo.bar.com }}]
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
  EXPECT_THROW_WITH_MESSAGE(
      StaticClusterImpl(cluster_config, runtime, factory_context, std::move(scope), false),
      EnvoyException,
      "malformed IP address: foo.bar.com. Consider setting resolver_name or "
      "setting cluster type to 'STRICT_DNS' or 'LOGICAL_DNS'");
}

TEST(ClusterDefinitionTest, BadClusterConfig) {
  const std::string json = R"EOF(
  {
    "name": "cluster_1",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "round_robin",
    "fake_type" : "expected_failure",
    "hosts": [{"url": "tcp://127.0.0.1:11001"}]
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_THROW(loader->validateSchema(Json::Schema::CLUSTER_SCHEMA), Json::Exception);
}

TEST(ClusterDefinitionTest, BadDnsClusterConfig) {
  const std::string json = R"EOF(
  {
    "name": "cluster_1",
    "connect_timeout_ms": 250,
    "type": "static",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://127.0.0.1:11001"}],
    "dns_lookup_family" : "foo"
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);
  EXPECT_THROW(loader->validateSchema(Json::Schema::CLUSTER_SCHEMA), Json::Exception);
}

TEST(StaticClusterImplTest, SourceAddressPriority) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockRandomGenerator> random;

  envoy::api::v2::Cluster config;
  config.set_name("staticcluster");
  config.mutable_connect_timeout();

  {
    // If the cluster manager gets a source address from the bootstrap proto, use it.
    NiceMock<MockClusterManager> cm;
    cm.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
    StaticClusterImpl cluster(config, runtime, factory_context, std::move(scope), false);
    EXPECT_EQ("1.2.3.5:0", cluster.info()->sourceAddress()->asString());
  }

  const std::string cluster_address = "5.6.7.8";
  config.mutable_upstream_bind_config()->mutable_source_address()->set_address(cluster_address);
  {
    // Verify source address from cluster config is used when present.
    NiceMock<MockClusterManager> cm;
    Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
    StaticClusterImpl cluster(config, runtime, factory_context, std::move(scope), false);
    EXPECT_EQ(cluster_address, cluster.info()->sourceAddress()->ip()->addressAsString());
  }

  {
    // The source address from cluster config takes precedence over one from the bootstrap proto.
    NiceMock<MockClusterManager> cm;
    cm.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);
    StaticClusterImpl cluster(config, runtime, factory_context, std::move(scope), false);
    EXPECT_EQ(cluster_address, cluster.info()->sourceAddress()->ip()->addressAsString());
  }
}

// Test that the correct feature() is set when close_connections_on_host_health_failure is
// configured.
TEST(ClusterImplTest, CloseConnectionsOnHostHealthFailure) {
  Stats::IsolatedStoreImpl stats;
  Ssl::MockContextManager ssl_context_manager;
  auto dns_resolver = std::make_shared<Network::MockDnsResolver>();
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<MockClusterManager> cm;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  NiceMock<Runtime::MockRandomGenerator> random;
  ReadyWatcher initialized;

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    close_connections_on_host_health_failure: true
    hosts: [{ socket_address: { address: foo.bar.com, port_value: 443 }}]
  )EOF";
  envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      ssl_context_manager, *scope, cm, local_info, dispatcher, random, stats);

  StrictDnsClusterImpl cluster(cluster_config, runtime, dns_resolver, factory_context,
                               std::move(scope), false);
  EXPECT_TRUE(cluster.info()->features() &
              ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE);
}

// Test creating and extending a priority set.
TEST(PrioritySet, Extend) {
  PrioritySetImpl priority_set;
  priority_set.getOrCreateHostSet(0);

  uint32_t changes = 0;
  uint32_t last_priority = 0;
  priority_set.addMemberUpdateCb([&](uint32_t priority, const HostVector&,
                                     const HostVector&) -> void { // fIXME
    last_priority = priority;
    ++changes;
  });

  // The initial priority set starts with priority level 0..
  EXPECT_EQ(1, priority_set.hostSetsPerPriority().size());
  EXPECT_EQ(0, priority_set.hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0, priority_set.hostSetsPerPriority()[0]->priority());

  // Add priorities 1 and 2, ensure the callback is called, and that the new
  // host sets are created with the correct priority.
  EXPECT_EQ(0, changes);
  EXPECT_EQ(0, priority_set.getOrCreateHostSet(2).hosts().size());
  EXPECT_EQ(3, priority_set.hostSetsPerPriority().size());
  // No-op host set creation does not trigger callbacks.
  EXPECT_EQ(0, changes);
  EXPECT_EQ(last_priority, 0);
  EXPECT_EQ(1, priority_set.hostSetsPerPriority()[1]->priority());
  EXPECT_EQ(2, priority_set.hostSetsPerPriority()[2]->priority());

  // Now add hosts for priority 1, and ensure they're added and subscribers are notified.
  std::shared_ptr<MockClusterInfo> info{new NiceMock<MockClusterInfo>()};
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info, "tcp://127.0.0.1:80")}));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostVector hosts_added{hosts->front()};
  HostVector hosts_removed{};

  priority_set.hostSetsPerPriority()[1]->updateHosts(hosts, hosts, hosts_per_locality,
                                                     hosts_per_locality, {}, hosts_added,
                                                     hosts_removed, absl::nullopt);
  EXPECT_EQ(1, changes);
  EXPECT_EQ(last_priority, 1);
  EXPECT_EQ(1, priority_set.hostSetsPerPriority()[1]->hosts().size());

  // Test iteration.
  int i = 0;
  for (auto& host_set : priority_set.hostSetsPerPriority()) {
    EXPECT_EQ(host_set.get(), priority_set.hostSetsPerPriority()[i++].get());
  }
}

class ClusterInfoImplTest : public testing::Test {
public:
  std::unique_ptr<StrictDnsClusterImpl> makeCluster(const std::string& yaml) {
    cluster_config_ = parseClusterFromV2Yaml(yaml);
    scope_ = stats_.createScope(fmt::format("cluster.{}.", cluster_config_.alt_stat_name().empty()
                                                               ? cluster_config_.name()
                                                               : cluster_config_.alt_stat_name()));
    factory_context_ = std::make_unique<Server::Configuration::TransportSocketFactoryContextImpl>(
        ssl_context_manager_, *scope_, cm_, local_info_, dispatcher_, random_, stats_);

    return std::make_unique<StrictDnsClusterImpl>(cluster_config_, runtime_, dns_resolver_,
                                                  *factory_context_, std::move(scope_), false);
  }

  Stats::IsolatedStoreImpl stats_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<Network::MockDnsResolver> dns_resolver_{new NiceMock<Network::MockDnsResolver>()};
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<MockClusterManager> cm_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  ReadyWatcher initialized_;
  envoy::api::v2::Cluster cluster_config_;
  Envoy::Stats::ScopePtr scope_;
  std::unique_ptr<Server::Configuration::TransportSocketFactoryContextImpl> factory_context_;
};

// Cluster metadata and common config retrieval.
TEST_F(ClusterInfoImplTest, Metadata) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: MAGLEV
    hosts: [{ socket_address: { address: foo.bar.com, port_value: 443 }}]
    metadata: { filter_metadata: { com.bar.foo: { baz: test_value } } }
    common_lb_config:
      healthy_panic_threshold:
        value: 0.3
  )EOF";

  auto cluster = makeCluster(yaml);

  EXPECT_EQ("test_value",
            Config::Metadata::metadataValue(cluster->info()->metadata(), "com.bar.foo", "baz")
                .string_value());
  EXPECT_EQ(0.3, cluster->info()->lbConfig().healthy_panic_threshold().value());
  EXPECT_EQ(LoadBalancerType::Maglev, cluster->info()->lbType());
}

// Cluster extension protocol options fails validation when configured for an unregistered filter.
TEST_F(ClusterInfoImplTest, ExtensionProtocolOptionsForUnknownFilter) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    hosts: [{ socket_address: { address: foo.bar.com, port_value: 443 }}]
    extension_protocol_options:
      no_such_filter: { option: value }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      makeCluster(yaml), EnvoyException,
      "Didn't find a registered network or http filter implementation for name: 'no_such_filter'");
}

class TestFilterConfigFactoryBase {
public:
  TestFilterConfigFactoryBase(
      std::function<ProtobufTypes::MessagePtr()> empty_proto,
      std::function<Upstream::ProtocolOptionsConfigConstSharedPtr(const Protobuf::Message&)> config)
      : empty_proto_(empty_proto), config_(config) {}

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() { return empty_proto_(); }
  Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsConfig(const Protobuf::Message& msg) {
    return config_(msg);
  }

  std::function<ProtobufTypes::MessagePtr()> empty_proto_;
  std::function<Upstream::ProtocolOptionsConfigConstSharedPtr(const Protobuf::Message&)> config_;
};

class TestNetworkFilterConfigFactory
    : public Server::Configuration::NamedNetworkFilterConfigFactory {
public:
  TestNetworkFilterConfigFactory(TestFilterConfigFactoryBase& parent) : parent_(parent) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb createFilterFactory(const Json::Object&,
                                               Server::Configuration::FactoryContext&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  Network::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&,
                               Server::Configuration::FactoryContext&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return parent_.createEmptyProtocolOptionsProto();
  }
  Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsConfig(const Protobuf::Message& msg) override {
    return parent_.createProtocolOptionsConfig(msg);
  }
  std::string name() override { CONSTRUCT_ON_FIRST_USE(std::string, "envoy.test.filter"); }

  TestFilterConfigFactoryBase& parent_;
};

class TestHttpFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  TestHttpFilterConfigFactory(TestFilterConfigFactoryBase& parent) : parent_(parent) {}

  // NamedNetworkFilterConfigFactory
  Http::FilterFactoryCb createFilterFactory(const Json::Object&, const std::string&,
                                            Server::Configuration::FactoryContext&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&,
                                  Server::Configuration::FactoryContext&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return parent_.createEmptyProtocolOptionsProto();
  }
  Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsConfig(const Protobuf::Message& msg) override {
    return parent_.createProtocolOptionsConfig(msg);
  }
  std::string name() override { CONSTRUCT_ON_FIRST_USE(std::string, "envoy.test.filter"); }

  TestFilterConfigFactoryBase& parent_;
};
struct TestFilterProtocolOptionsConfig : public Upstream::ProtocolOptionsConfig {};

// Cluster extension protocol options fails validation when configured for filter that does not
// support options.
TEST_F(ClusterInfoImplTest, ExtensionProtocolOptionsForFilterWithoutOptions) {
  TestFilterConfigFactoryBase factoryBase(
      []() -> ProtobufTypes::MessagePtr { return nullptr; },
      [](const Protobuf::Message&) -> Upstream::ProtocolOptionsConfigConstSharedPtr {
        return nullptr;
      });
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    hosts: [{ socket_address: { address: foo.bar.com, port_value: 443 }}]
    extension_protocol_options:
      envoy.test.filter: { option: value }
  )EOF";

  {
    TestNetworkFilterConfigFactory factory(factoryBase);
    Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> registry(
        factory);
    EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml), EnvoyException,
                              "filter envoy.test.filter does not support protocol options");
  }
  {
    TestHttpFilterConfigFactory factory(factoryBase);
    Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registry(factory);
    EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml), EnvoyException,
                              "filter envoy.test.filter does not support protocol options");
  }
}

// Cluster retrieval of typed extension protocol options.
TEST_F(ClusterInfoImplTest, ExtensionProtocolOptionsForFilterWithOptions) {
  auto protocol_options = std::make_shared<TestFilterProtocolOptionsConfig>();

  TestFilterConfigFactoryBase factoryBase(
      []() -> ProtobufTypes::MessagePtr { return std::make_unique<ProtobufWkt::Struct>(); },
      [&](const Protobuf::Message& msg) -> Upstream::ProtocolOptionsConfigConstSharedPtr {
        const auto& msg_struct = dynamic_cast<const ProtobufWkt::Struct&>(msg);
        EXPECT_TRUE(msg_struct.fields().find("option") != msg_struct.fields().end());

        return protocol_options;
      });

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    hosts: [{ socket_address: { address: foo.bar.com, port_value: 443 }}]
    extension_protocol_options:
      envoy.test.filter: { option: "value" }
  )EOF";

  // This vector is used to gather clusters with extension_protocol_options from the different
  // types of extension factories (network, http).
  std::vector<std::unique_ptr<StrictDnsClusterImpl>> clusters;

  {
    // Get the cluster with extension_protocol_options for a network filter factory.
    TestNetworkFilterConfigFactory factory(factoryBase);
    Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> registry(
        factory);
    clusters.push_back(makeCluster(yaml));
  }
  {
    // Get the cluster with extension_protocol_options for an http filter factory.
    TestHttpFilterConfigFactory factory(factoryBase);
    Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registry(factory);
    clusters.push_back(makeCluster(yaml));
  }

  // Make sure that the clusters created from both factories are as expected.
  for (auto&& cluster : clusters) {
    std::shared_ptr<const TestFilterProtocolOptionsConfig> stored_options =
        cluster->info()->extensionProtocolOptionsTyped<TestFilterProtocolOptionsConfig>(
            "envoy.test.filter");
    EXPECT_NE(nullptr, protocol_options);
    // Same pointer
    EXPECT_EQ(stored_options.get(), protocol_options.get());
  }
}

// Validate empty singleton for HostsPerLocalityImpl.
TEST(HostsPerLocalityImpl, Empty) {
  EXPECT_FALSE(HostsPerLocalityImpl::empty()->hasLocalLocality());
  EXPECT_EQ(0, HostsPerLocalityImpl::empty()->get().size());
}

// Validate HostsPerLocalityImpl constructors.
TEST(HostsPerLocalityImpl, Cons) {
  {
    const HostsPerLocalityImpl hosts_per_locality;
    EXPECT_FALSE(hosts_per_locality.hasLocalLocality());
    EXPECT_EQ(0, hosts_per_locality.get().size());
  }

  MockCluster cluster;
  HostSharedPtr host_0 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 1);
  HostSharedPtr host_1 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 1);

  {
    std::vector<HostVector> locality_hosts = {{host_0}, {host_1}};
    const auto locality_hosts_copy = locality_hosts;
    const HostsPerLocalityImpl hosts_per_locality(std::move(locality_hosts), true);
    EXPECT_TRUE(hosts_per_locality.hasLocalLocality());
    EXPECT_EQ(locality_hosts_copy, hosts_per_locality.get());
  }

  {
    std::vector<HostVector> locality_hosts = {{host_0}, {host_1}};
    const auto locality_hosts_copy = locality_hosts;
    const HostsPerLocalityImpl hosts_per_locality(std::move(locality_hosts), false);
    EXPECT_FALSE(hosts_per_locality.hasLocalLocality());
    EXPECT_EQ(locality_hosts_copy, hosts_per_locality.get());
  }
}

TEST(HostsPerLocalityImpl, Filter) {
  MockCluster cluster;
  HostSharedPtr host_0 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 1);
  HostSharedPtr host_1 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 1);

  {
    std::vector<HostVector> locality_hosts = {{host_0}, {host_1}};
    const auto filtered =
        HostsPerLocalityImpl(std::move(locality_hosts), false).filter([&host_0](const Host& host) {
          return &host == host_0.get();
        });
    EXPECT_FALSE(filtered->hasLocalLocality());
    const std::vector<HostVector> expected_locality_hosts = {{host_0}, {}};
    EXPECT_EQ(expected_locality_hosts, filtered->get());
  }

  {
    std::vector<HostVector> locality_hosts = {{host_0}, {host_1}};
    auto filtered =
        HostsPerLocalityImpl(std::move(locality_hosts), true).filter([&host_1](const Host& host) {
          return &host == host_1.get();
        });
    EXPECT_TRUE(filtered->hasLocalLocality());
    const std::vector<HostVector> expected_locality_hosts = {{}, {host_1}};
    EXPECT_EQ(expected_locality_hosts, filtered->get());
  }
}

class HostSetImplLocalityTest : public ::testing::Test {
public:
  LocalityWeightsConstSharedPtr locality_weights_;
  HostSetImpl host_set_{0, kDefaultOverProvisioningFactor};
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  HostVector hosts_{
      makeTestHost(info_, "tcp://127.0.0.1:80"), makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"), makeTestHost(info_, "tcp://127.0.0.1:83"),
      makeTestHost(info_, "tcp://127.0.0.1:84"), makeTestHost(info_, "tcp://127.0.0.1:85")};
};

// When no locality weights belong to the host set, there's an empty pick.
TEST_F(HostSetImplLocalityTest, Empty) {
  EXPECT_EQ(nullptr, host_set_.localityWeights());
  EXPECT_FALSE(host_set_.chooseLocality().has_value());
}

// When no hosts are healthy we should fail to select a locality
TEST_F(HostSetImplLocalityTest, AllUnhealthy) {
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}, {hosts_[2]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1, 1}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(hosts, std::make_shared<const HostVector>(), hosts_per_locality,
                        hosts_per_locality, locality_weights, {}, {}, absl::nullopt);
  EXPECT_FALSE(host_set_.chooseLocality().has_value());
}

// When all locality weights are zero we should fail to select a locality.
TEST_F(HostSetImplLocalityTest, AllZeroWeights) {
  HostsPerLocalitySharedPtr hosts_per_locality = makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{0, 0}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(hosts, hosts, hosts_per_locality, hosts_per_locality, locality_weights, {},
                        {});
  EXPECT_FALSE(host_set_.chooseLocality().has_value());
}

// When all locality weights are the same we have unweighted RR behavior.
TEST_F(HostSetImplLocalityTest, Unweighted) {
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}, {hosts_[2]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1, 1}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(hosts, hosts, hosts_per_locality, hosts_per_locality, locality_weights, {},
                        {}, absl::nullopt);
  EXPECT_EQ(0, host_set_.chooseLocality().value());
  EXPECT_EQ(1, host_set_.chooseLocality().value());
  EXPECT_EQ(2, host_set_.chooseLocality().value());
  EXPECT_EQ(0, host_set_.chooseLocality().value());
  EXPECT_EQ(1, host_set_.chooseLocality().value());
  EXPECT_EQ(2, host_set_.chooseLocality().value());
}

// When locality weights differ, we have weighted RR behavior.
TEST_F(HostSetImplLocalityTest, Weighted) {
  HostsPerLocalitySharedPtr hosts_per_locality = makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 2}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(hosts, hosts, hosts_per_locality, hosts_per_locality, locality_weights, {},
                        {}, absl::nullopt);
  EXPECT_EQ(1, host_set_.chooseLocality().value());
  EXPECT_EQ(0, host_set_.chooseLocality().value());
  EXPECT_EQ(1, host_set_.chooseLocality().value());
  EXPECT_EQ(1, host_set_.chooseLocality().value());
  EXPECT_EQ(0, host_set_.chooseLocality().value());
  EXPECT_EQ(1, host_set_.chooseLocality().value());
}

// Localities with no weight assignment are never picked.
TEST_F(HostSetImplLocalityTest, MissingWeight) {
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}, {hosts_[2]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 0, 1}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(hosts, hosts, hosts_per_locality, hosts_per_locality, locality_weights, {},
                        {}, absl::nullopt);
  EXPECT_EQ(0, host_set_.chooseLocality().value());
  EXPECT_EQ(2, host_set_.chooseLocality().value());
  EXPECT_EQ(0, host_set_.chooseLocality().value());
  EXPECT_EQ(2, host_set_.chooseLocality().value());
  EXPECT_EQ(0, host_set_.chooseLocality().value());
  EXPECT_EQ(2, host_set_.chooseLocality().value());
}

// Gentle failover between localities as health diminishes.
TEST_F(HostSetImplLocalityTest, UnhealthyFailover) {
  const auto setHealthyHostCount = [this](uint32_t host_count) {
    LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 2}};
    HostsPerLocalitySharedPtr hosts_per_locality = makeHostsPerLocality(
        {{hosts_[0], hosts_[1], hosts_[2], hosts_[3], hosts_[4]}, {hosts_[5]}});
    HostVector healthy_hosts;
    for (uint32_t i = 0; i < host_count; ++i) {
      healthy_hosts.emplace_back(hosts_[i]);
    }
    HostsPerLocalitySharedPtr healthy_hosts_per_locality =
        makeHostsPerLocality({healthy_hosts, {hosts_[5]}});

    auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
    host_set_.updateHosts(makeHostsFromHostsPerLocality(hosts_per_locality),
                          makeHostsFromHostsPerLocality(healthy_hosts_per_locality),
                          hosts_per_locality, healthy_hosts_per_locality, locality_weights, {}, {},
                          absl::nullopt);
  };

  const auto expectPicks = [this](uint32_t locality_0_picks, uint32_t locality_1_picks) {
    uint32_t count[2] = {0, 0};
    for (uint32_t i = 0; i < 100; ++i) {
      const uint32_t locality_index = host_set_.chooseLocality().value();
      ASSERT_LT(locality_index, 2);
      ++count[locality_index];
    }
    ENVOY_LOG_MISC(debug, "Locality picks {} {}", count[0], count[1]);
    EXPECT_EQ(locality_0_picks, count[0]);
    EXPECT_EQ(locality_1_picks, count[1]);
  };

  setHealthyHostCount(5);
  expectPicks(33, 67);
  setHealthyHostCount(4);
  expectPicks(33, 67);
  setHealthyHostCount(3);
  expectPicks(29, 71);
  setHealthyHostCount(2);
  expectPicks(22, 78);
  setHealthyHostCount(1);
  expectPicks(12, 88);
  setHealthyHostCount(0);
  expectPicks(0, 100);
}

TEST(OverProvisioningFactorTest, LocalityPickChanges) {
  auto setUpHostSetWithOPFAndTestPicks = [](const uint32_t overprovisioning_factor,
                                            const uint32_t pick_0, const uint32_t pick_1) {
    HostSetImpl host_set(0, overprovisioning_factor);
    std::shared_ptr<MockClusterInfo> cluster_info{new NiceMock<MockClusterInfo>()};
    HostVector hosts{makeTestHost(cluster_info, "tcp://127.0.0.1:80"),
                     makeTestHost(cluster_info, "tcp://127.0.0.1:81"),
                     makeTestHost(cluster_info, "tcp://127.0.0.1:82")};
    LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1}};
    HostsPerLocalitySharedPtr hosts_per_locality =
        makeHostsPerLocality({{hosts[0], hosts[1]}, {hosts[2]}});
    // Healthy ratio: (1/2, 1).
    HostsPerLocalitySharedPtr healthy_hosts_per_locality =
        makeHostsPerLocality({{hosts[0]}, {hosts[2]}});
    host_set.updateHosts(makeHostsFromHostsPerLocality(hosts_per_locality),
                         makeHostsFromHostsPerLocality(healthy_hosts_per_locality),
                         hosts_per_locality, healthy_hosts_per_locality, locality_weights, {}, {},
                         absl::nullopt);
    uint32_t cnts[] = {0, 0};
    for (uint32_t i = 0; i < 100; ++i) {
      absl::optional<uint32_t> locality_index = host_set.chooseLocality();
      if (!locality_index.has_value()) {
        // It's possible locality scheduler is nullptr (when factor is 0).
        continue;
      }
      ASSERT_LT(locality_index.value(), 2);
      ++cnts[locality_index.value()];
    }
    EXPECT_EQ(pick_0, cnts[0]);
    EXPECT_EQ(pick_1, cnts[1]);
  };

  // NOTE: effective locality weight: weight * min(1, factor * healthy-ratio).

  // Picks in localities match to weight(1) * healthy-ratio when
  // overprovisioning factor is 1.
  setUpHostSetWithOPFAndTestPicks(100, 33, 67);
  // Picks in localities match to weights as factor * healthy-ratio > 1.
  setUpHostSetWithOPFAndTestPicks(200, 50, 50);
};

} // namespace
} // namespace Upstream
} // namespace Envoy
