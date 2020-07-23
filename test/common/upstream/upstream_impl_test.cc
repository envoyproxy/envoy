#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/http/codec.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/config/metadata.h"
#include "common/network/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/static_cluster.h"
#include "common/upstream/strict_dns_cluster.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
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
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

class UpstreamImplTestBase {
protected:
  UpstreamImplTestBase() : api_(Api::createApiForTest(stats_)) {}

  NiceMock<Server::MockAdmin> admin_;
  Ssl::MockContextManager ssl_context_manager_;
  NiceMock<MockClusterManager> cm_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::TestUtil::TestStore stats_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
};

std::list<std::string> hostListToAddresses(const HostVector& hosts) {
  std::list<std::string> addresses;
  for (const HostSharedPtr& host : hosts) {
    addresses.push_back(host->address()->asString());
  }

  return addresses;
}

template <class HostsT = HostVector>
std::shared_ptr<const HostsT>
makeHostsFromHostsPerLocality(HostsPerLocalityConstSharedPtr hosts_per_locality) {
  HostVector hosts;

  for (const auto& locality_hosts : hosts_per_locality->get()) {
    for (const auto& host : locality_hosts) {
      hosts.emplace_back(host);
    }
  }

  return std::make_shared<const HostsT>(hosts);
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

using StrictDnsConfigTuple =
    std::tuple<std::string, Network::DnsLookupFamily, std::list<std::string>>;
std::vector<StrictDnsConfigTuple> generateStrictDnsParams() {
  std::vector<StrictDnsConfigTuple> dns_config;
  {
    std::string family_yaml("");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::Auto);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response));
  }
  {
    std::string family_yaml(R"EOF(dns_lookup_family: v4_only
                            )EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V4Only);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response));
  }
  {
    std::string family_yaml(R"EOF(dns_lookup_family: v6_only
                            )EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::V6Only);
    std::list<std::string> dns_response{"::1", "::2"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response));
  }
  {
    std::string family_yaml(R"EOF(dns_lookup_family: auto
                            )EOF");
    Network::DnsLookupFamily family(Network::DnsLookupFamily::Auto);
    std::list<std::string> dns_response{"127.0.0.1", "127.0.0.2"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response));
  }
  return dns_config;
}

class StrictDnsParamTest : public testing::TestWithParam<StrictDnsConfigTuple>,
                           public UpstreamImplTestBase {};

INSTANTIATE_TEST_SUITE_P(DnsParam, StrictDnsParamTest,
                         testing::ValuesIn(generateStrictDnsParams()));

TEST_P(StrictDnsParamTest, ImmediateResolve) {
  auto dns_resolver = std::make_shared<NiceMock<Network::MockDnsResolver>>();
  ReadyWatcher initialized;
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: strict_dns
    )EOF" + std::get<0>(GetParam()) +
                           R"EOF(
    lb_policy: round_robin
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
  )EOF";
  EXPECT_CALL(initialized, ready());
  EXPECT_CALL(*dns_resolver, resolve("foo.bar.com", std::get<1>(GetParam()), _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        cb(Network::DnsResolver::ResolutionStatus::Success,
           TestUtility::makeDnsResponse(std::get<2>(GetParam())));
        return nullptr;
      }));
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);

  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver, factory_context,
                               std::move(scope), false);
  cluster.initialize([&]() -> void { initialized.ready(); });
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

class StrictDnsClusterImplTest : public testing::Test, public UpstreamImplTestBase {
protected:
  std::shared_ptr<Network::MockDnsResolver> dns_resolver_ =
      std::make_shared<Network::MockDnsResolver>();
};

TEST_F(StrictDnsClusterImplTest, ZeroHostsIsInializedImmediately) {
  ReadyWatcher initialized;

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
      endpoints:
      - lb_endpoints:
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver_, factory_context,
                               std::move(scope), false);
  EXPECT_CALL(initialized, ready());
  cluster.initialize([&]() -> void { initialized.ready(); });
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

// Resolve zero hosts, while using health checking.
TEST_F(StrictDnsClusterImplTest, ZeroHostsHealthChecker) {
  ReadyWatcher initialized;

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
  )EOF";

  ResolverData resolver(*dns_resolver_, dispatcher_);
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver_, factory_context,
                               std::move(scope), false);
  std::shared_ptr<MockHealthChecker> health_checker(new MockHealthChecker());
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  cluster.setHealthChecker(health_checker);
  cluster.initialize([&]() -> void { initialized.ready(); });

  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(initialized, ready());
  EXPECT_CALL(*resolver.timer_, enableTimer(_, _));
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success, {});
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

TEST_F(StrictDnsClusterImplTest, Basic) {
  // gmock matches in LIFO order which is why these are swapped.
  ResolverData resolver2(*dns_resolver_, dispatcher_);
  ResolverData resolver1(*dns_resolver_, dispatcher_);

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: strict_dns
    dns_refresh_rate: 4s
    dns_failure_refresh_rate:
      base_interval: 7s
      max_interval: 10s
    lb_policy: round_robin
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
    protocol_selection: USE_DOWNSTREAM_PROTOCOL
    http2_protocol_options:
      hpack_table_size: 0
    http_protocol_options:
      header_key_format:
        proper_case_words: {}
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: localhost1
                    port_value: 11001
            - endpoint:
                address:
                  socket_address:
                    address: localhost2
                    port_value: 11002 
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver_, factory_context,
                               std::move(scope), false);
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.default.max_connections", 43));
  EXPECT_EQ(43U, cluster.info()->resourceManager(ResourcePriority::Default).connections().max());
  EXPECT_CALL(runtime_.snapshot_,
              getInteger("circuit_breakers.name.default.max_pending_requests", 57));
  EXPECT_EQ(57U,
            cluster.info()->resourceManager(ResourcePriority::Default).pendingRequests().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.default.max_requests", 50));
  EXPECT_EQ(50U, cluster.info()->resourceManager(ResourcePriority::Default).requests().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.default.max_retries", 10));
  EXPECT_EQ(10U, cluster.info()->resourceManager(ResourcePriority::Default).retries().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.high.max_connections", 1));
  EXPECT_EQ(1U, cluster.info()->resourceManager(ResourcePriority::High).connections().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.high.max_pending_requests", 2));
  EXPECT_EQ(2U, cluster.info()->resourceManager(ResourcePriority::High).pendingRequests().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.high.max_requests", 3));
  EXPECT_EQ(3U, cluster.info()->resourceManager(ResourcePriority::High).requests().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.high.max_retries", 4));
  EXPECT_EQ(4U, cluster.info()->resourceManager(ResourcePriority::High).retries().max());
  EXPECT_EQ(3U, cluster.info()->maxRequestsPerConnection());
  EXPECT_EQ(0U, cluster.info()->http2Options().hpack_table_size().value());
  EXPECT_EQ(Http::Http1Settings::HeaderKeyFormat::ProperCase,
            cluster.info()->http1Settings().header_key_format_);

  cluster.info()->stats().upstream_rq_total_.inc();
  EXPECT_EQ(1UL, stats_.counter("cluster.name.upstream_rq_total").value());

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.maintenance_mode.name", 0));
  EXPECT_FALSE(cluster.info()->maintenanceMode());

  ReadyWatcher membership_updated;
  cluster.prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) -> void { membership_updated.ready(); });

  cluster.initialize([] {});

  resolver1.expectResolve(*dns_resolver_);
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->hostname());

  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.3"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  // Make sure we de-dup the same address.
  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver2.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.1"}));
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

  // Empty response. With successful but empty response the host list deletes the address.
  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({}));
  EXPECT_THAT(
      std::list<std::string>({"10.0.0.1:11002"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  // Empty response. With failing but empty response the host list does not delete the address.
  ON_CALL(random_, random()).WillByDefault(Return(8000));
  resolver2.expectResolve(*dns_resolver_);
  resolver2.timer_->invokeCallback();
  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(1000), _));
  resolver2.dns_callback_(Network::DnsResolver::ResolutionStatus::Failure,
                          TestUtility::makeDnsResponse({}));
  EXPECT_THAT(
      std::list<std::string>({"10.0.0.1:11002"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  // Make sure we cancel.
  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  resolver2.expectResolve(*dns_resolver_);
  resolver2.timer_->invokeCallback();

  EXPECT_CALL(resolver1.active_dns_query_, cancel());
  EXPECT_CALL(resolver2.active_dns_query_, cancel());
}

// Verifies that host removal works correctly when hosts are being health checked
// but the cluster is configured to always remove hosts
TEST_F(StrictDnsClusterImplTest, HostRemovalActiveHealthSkipped) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    ignore_health_on_host_removal: true
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443 
  )EOF";

  ResolverData resolver(*dns_resolver_, dispatcher_);
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver_, factory_context,
                               std::move(scope), false);
  std::shared_ptr<MockHealthChecker> health_checker(new MockHealthChecker());
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  cluster.setHealthChecker(health_checker);
  cluster.initialize([&]() -> void {});

  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(*resolver.timer_, enableTimer(_, _)).Times(2);
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                         TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // Verify that both endpoints are initially marked with FAILED_ACTIVE_HC, then
  // clear the flag to simulate that these endpoints have been successfully health
  // checked.
  {
    const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(2UL, hosts.size());

    for (const auto& host : hosts) {
      EXPECT_TRUE(host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
      host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
      host->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
    }
  }

  // Re-resolve the DNS name with only one record
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                         TestUtility::makeDnsResponse({"127.0.0.1"}));

  const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(1UL, hosts.size());
}

// Verify that a host is not removed if it is removed from DNS but still passing active health
// checking.
TEST_F(StrictDnsClusterImplTest, HostRemovalAfterHcFail) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
  )EOF";

  ResolverData resolver(*dns_resolver_, dispatcher_);
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver_, factory_context,
                               std::move(scope), false);
  std::shared_ptr<MockHealthChecker> health_checker(new MockHealthChecker());
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  cluster.setHealthChecker(health_checker);
  ReadyWatcher initialized;
  cluster.initialize([&initialized]() { initialized.ready(); });

  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(*resolver.timer_, enableTimer(_, _)).Times(2);
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                         TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // Verify that both endpoints are initially marked with FAILED_ACTIVE_HC, then
  // clear the flag to simulate that these endpoints have been successfully health
  // checked.
  {
    const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(2UL, hosts.size());

    for (size_t i = 0; i < 2; ++i) {
      EXPECT_TRUE(hosts[i]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
      hosts[i]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
      hosts[i]->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
      if (i == 1) {
        EXPECT_CALL(initialized, ready());
      }
      health_checker->runCallbacks(hosts[i], HealthTransition::Changed);
    }
  }

  // Re-resolve the DNS name with only one record, we should still have 2 hosts.
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                         TestUtility::makeDnsResponse({"127.0.0.1"}));

  {
    const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(2UL, hosts.size());
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));

    hosts[1]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
    health_checker->runCallbacks(hosts[1], HealthTransition::Changed);
  }

  // Unlike EDS we will not remove if HC is failing but will wait until the next polling interval.
  // This may change in the future.
  {
    const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(2UL, hosts.size());
  }
}

TEST_F(StrictDnsClusterImplTest, LoadAssignmentBasic) {
  // gmock matches in LIFO order which is why these are swapped.
  ResolverData resolver3(*dns_resolver_, dispatcher_);
  ResolverData resolver2(*dns_resolver_, dispatcher_);
  ResolverData resolver1(*dns_resolver_, dispatcher_);

  const std::string yaml = R"EOF(
    name: name
    type: STRICT_DNS

    dns_lookup_family: V4_ONLY
    connect_timeout: 0.25s
    dns_refresh_rate: 4s
    dns_failure_refresh_rate:
      base_interval: 7s
      max_interval: 10s

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
      policy:
        overprovisioning_factor: 100
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: localhost1
                port_value: 11001
            health_check_config:
              port_value: 8000
          health_status: DEGRADED
        - endpoint:
            address:
              socket_address:
                address: localhost2
                port_value: 11002
            health_check_config:
              port_value: 8000
        - endpoint:
            address:
              socket_address:
                address: localhost3
                port_value: 11002
            health_check_config:
              port_value: 8000
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver_, factory_context,
                               std::move(scope), false);

  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.default.max_connections", 43));
  EXPECT_EQ(43U, cluster.info()->resourceManager(ResourcePriority::Default).connections().max());
  EXPECT_CALL(runtime_.snapshot_,
              getInteger("circuit_breakers.name.default.max_pending_requests", 57));
  EXPECT_EQ(57U,
            cluster.info()->resourceManager(ResourcePriority::Default).pendingRequests().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.default.max_requests", 50));
  EXPECT_EQ(50U, cluster.info()->resourceManager(ResourcePriority::Default).requests().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.default.max_retries", 10));
  EXPECT_EQ(10U, cluster.info()->resourceManager(ResourcePriority::Default).retries().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.high.max_connections", 1));
  EXPECT_EQ(1U, cluster.info()->resourceManager(ResourcePriority::High).connections().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.high.max_pending_requests", 2));
  EXPECT_EQ(2U, cluster.info()->resourceManager(ResourcePriority::High).pendingRequests().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.high.max_requests", 3));
  EXPECT_EQ(3U, cluster.info()->resourceManager(ResourcePriority::High).requests().max());
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.high.max_retries", 4));
  EXPECT_EQ(4U, cluster.info()->resourceManager(ResourcePriority::High).retries().max());
  EXPECT_EQ(3U, cluster.info()->maxRequestsPerConnection());
  EXPECT_EQ(0U, cluster.info()->http2Options().hpack_table_size().value());

  cluster.info()->stats().upstream_rq_total_.inc();
  EXPECT_EQ(1UL, stats_.counter("cluster.name.upstream_rq_total").value());

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.maintenance_mode.name", 0));
  EXPECT_FALSE(cluster.info()->maintenanceMode());

  ReadyWatcher membership_updated;
  cluster.prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) -> void { membership_updated.ready(); });

  cluster.initialize([] {});

  resolver1.expectResolve(*dns_resolver_);
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->hostname());
  EXPECT_EQ(100, cluster.prioritySet().hostSetsPerPriority()[0]->overprovisioningFactor());
  EXPECT_EQ(Host::Health::Degraded,
            cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->health());
  EXPECT_EQ(Host::Health::Degraded,
            cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->health());

  // This is the first time we received an update for localhost1, we expect to rebuild.
  EXPECT_EQ(0UL, stats_.counter("cluster.name.update_no_rebuild").value());

  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));
  EXPECT_EQ(100, cluster.prioritySet().hostSetsPerPriority()[0]->overprovisioningFactor());

  // Since no change for localhost1, we expect no rebuild.
  EXPECT_EQ(1UL, stats_.counter("cluster.name.update_no_rebuild").value());

  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));
  EXPECT_EQ(100, cluster.prioritySet().hostSetsPerPriority()[0]->overprovisioningFactor());

  // Since no change for localhost1, we expect no rebuild.
  EXPECT_EQ(2UL, stats_.counter("cluster.name.update_no_rebuild").value());

  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver2.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.1"}));

  // We received a new set of hosts for localhost2. Should rebuild the cluster.
  EXPECT_EQ(2UL, stats_.counter("cluster.name.update_no_rebuild").value());

  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));

  // We again received the same set as before for localhost1. No rebuild this time.
  EXPECT_EQ(3UL, stats_.counter("cluster.name.update_no_rebuild").value());

  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.3"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001", "10.0.0.1:11002"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  // Make sure we de-dup the same address.
  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver2.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001", "10.0.0.1:11002"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(1UL,
            cluster.prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // Make sure that we *don't* de-dup between resolve targets.
  EXPECT_CALL(*resolver3.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver3.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"10.0.0.1"}));

  const auto hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_THAT(std::list<std::string>({"127.0.0.3:11001", "10.0.0.1:11002", "10.0.0.1:11002"}),
              ContainerEq(hostListToAddresses(hosts)));

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(1UL,
            cluster.prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // Ensure that all host objects in the host list are unique.
  for (const auto& host : hosts) {
    EXPECT_EQ(1, std::count(hosts.begin(), hosts.end(), host));
  }

  for (const HostSharedPtr& host : cluster.prioritySet().hostSetsPerPriority()[0]->hosts()) {
    EXPECT_EQ(cluster.info().get(), &host->cluster());
  }

  // Remove the duplicated hosts from both resolve targets and ensure that we don't see the same
  // host multiple times.
  std::unordered_set<HostSharedPtr> removed_hosts;
  cluster.prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector& hosts_removed) -> void {
        for (const auto& host : hosts_removed) {
          EXPECT_EQ(removed_hosts.end(), removed_hosts.find(host));
          removed_hosts.insert(host);
        }
      });

  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver2.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({}));

  EXPECT_CALL(*resolver3.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver3.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({}));

  // Ensure that we called the update membership callback.
  EXPECT_EQ(2, removed_hosts.size());

  // Make sure we cancel.
  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  resolver2.expectResolve(*dns_resolver_);
  resolver2.timer_->invokeCallback();
  resolver3.expectResolve(*dns_resolver_);
  resolver3.timer_->invokeCallback();

  EXPECT_CALL(resolver1.active_dns_query_, cancel());
  EXPECT_CALL(resolver2.active_dns_query_, cancel());
  EXPECT_CALL(resolver3.active_dns_query_, cancel());
}

TEST_F(StrictDnsClusterImplTest, LoadAssignmentBasicMultiplePriorities) {
  ResolverData resolver3(*dns_resolver_, dispatcher_);
  ResolverData resolver2(*dns_resolver_, dispatcher_);
  ResolverData resolver1(*dns_resolver_, dispatcher_);

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

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver_, factory_context,
                               std::move(scope), false);
  ReadyWatcher membership_updated;
  cluster.prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) -> void { membership_updated.ready(); });

  cluster.initialize([] {});

  resolver1.expectResolve(*dns_resolver_);
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->hostname());

  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.1:11001", "127.0.0.2:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  resolver1.timer_->invokeCallback();
  EXPECT_CALL(*resolver1.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver1.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"127.0.0.3"}));
  EXPECT_THAT(
      std::list<std::string>({"127.0.0.3:11001"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[0]->hosts())));

  // Make sure we de-dup the same address.
  EXPECT_CALL(*resolver2.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver2.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"10.0.0.1", "10.0.0.1"}));
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

  EXPECT_CALL(*resolver3.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver3.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                          TestUtility::makeDnsResponse({"192.168.1.1", "192.168.1.2"}));

  // Make sure we have multiple priorities.
  EXPECT_THAT(
      std::list<std::string>({"192.168.1.1:11003", "192.168.1.2:11003"}),
      ContainerEq(hostListToAddresses(cluster.prioritySet().hostSetsPerPriority()[1]->hosts())));

  // Make sure we cancel.
  resolver1.expectResolve(*dns_resolver_);
  resolver1.timer_->invokeCallback();
  resolver2.expectResolve(*dns_resolver_);
  resolver2.timer_->invokeCallback();
  resolver3.expectResolve(*dns_resolver_);
  resolver3.timer_->invokeCallback();

  EXPECT_CALL(resolver1.active_dns_query_, cancel());
  EXPECT_CALL(resolver2.active_dns_query_, cancel());
  EXPECT_CALL(resolver3.active_dns_query_, cancel());
}

// Verifies that specifying a custom resolver when using STRICT_DNS fails
TEST_F(StrictDnsClusterImplTest, CustomResolverFails) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    ignore_health_on_host_removal: true
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
                    resolver_name: customresolver 
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope =
      stats_.createScope(fmt::format("cluster.{}.", cluster_config.name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);

  EXPECT_THROW_WITH_MESSAGE(
      std::make_unique<StrictDnsClusterImpl>(cluster_config, runtime_, dns_resolver_,
                                             factory_context, std::move(scope), false),
      EnvoyException, "STRICT_DNS clusters must NOT have a custom resolver name set");
}

TEST_F(StrictDnsClusterImplTest, FailureRefreshRateBackoffResetsWhenSuccessHappens) {
  ResolverData resolver(*dns_resolver_, dispatcher_);

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    dns_refresh_rate: 4s
    dns_failure_refresh_rate:
      base_interval: 7s
      max_interval: 10s
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: localhost1
                    port_value: 11001
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver_, factory_context,
                               std::move(scope), false);
  cluster.initialize([] {});

  // Failing response kicks the failure refresh backoff strategy.
  ON_CALL(random_, random()).WillByDefault(Return(8000));
  EXPECT_CALL(*resolver.timer_, enableTimer(std::chrono::milliseconds(1000), _));
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Failure,
                         TestUtility::makeDnsResponse({}));

  // Successful call should reset the failure backoff strategy.
  EXPECT_CALL(*resolver.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                         TestUtility::makeDnsResponse({}));

  // Therefore, a subsequent failure should get a [0,base * 1] refresh.
  ON_CALL(random_, random()).WillByDefault(Return(8000));
  EXPECT_CALL(*resolver.timer_, enableTimer(std::chrono::milliseconds(1000), _));
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Failure,
                         TestUtility::makeDnsResponse({}));
}

TEST_F(StrictDnsClusterImplTest, TtlAsDnsRefreshRate) {
  ResolverData resolver(*dns_resolver_, dispatcher_);

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    dns_refresh_rate: 4s
    respect_dns_ttl: true
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: localhost1
                    port_value: 11001
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver_, factory_context,
                               std::move(scope), false);
  ReadyWatcher membership_updated;
  cluster.prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) -> void { membership_updated.ready(); });

  cluster.initialize([] {});

  // TTL is recorded when the DNS response is successful and not empty
  EXPECT_CALL(membership_updated, ready());
  EXPECT_CALL(*resolver.timer_, enableTimer(std::chrono::milliseconds(5000), _));
  resolver.dns_callback_(
      Network::DnsResolver::ResolutionStatus::Success,
      TestUtility::makeDnsResponse({"192.168.1.1", "192.168.1.2"}, std::chrono::seconds(5)));

  // If the response is successful but empty, the cluster uses the cluster configured refresh rate.
  EXPECT_CALL(membership_updated, ready());
  EXPECT_CALL(*resolver.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                         TestUtility::makeDnsResponse({}, std::chrono::seconds(5)));

  // On failure, the cluster uses the cluster configured refresh rate.
  EXPECT_CALL(*resolver.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Failure,
                         TestUtility::makeDnsResponse({}, std::chrono::seconds(5)));
}

// Ensures that HTTP/2 user defined SETTINGS parameter validation is enforced on clusters.
TEST_F(StrictDnsClusterImplTest, Http2UserDefinedSettingsParametersValidation) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: strict_dns
    dns_refresh_rate: 4s
    dns_failure_refresh_rate:
      base_interval: 7s
      max_interval: 10s
    lb_policy: round_robin
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
    protocol_selection: USE_DOWNSTREAM_PROTOCOL
    http2_protocol_options:
      hpack_table_size: 2048
      custom_settings_parameters: { identifier: 1, value: 1024 }
    http_protocol_options:
      header_key_format:
        proper_case_words: {}
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: localhost1
                    port_value: 11001                     
            - endpoint:
                address:
                  socket_address:
                    address: localhost2
                    port_value: 11002
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  EXPECT_THROW_WITH_REGEX(
      StrictDnsClusterImpl(cluster_config, runtime_, dns_resolver_, factory_context,
                           std::move(scope), false),
      EnvoyException,
      R"(the \{hpack_table_size\} HTTP/2 SETTINGS parameter\(s\) can not be configured through)"
      " both");
}

TEST(HostImplTest, HostCluster) {
  MockClusterMockPrioritySet cluster;
  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 1);
  EXPECT_EQ(cluster.info_.get(), &host->cluster());
  EXPECT_EQ("", host->hostname());
  EXPECT_FALSE(host->canary());
  EXPECT_EQ("", host->locality().zone());
}

TEST(HostImplTest, Weight) {
  MockClusterMockPrioritySet cluster;

  EXPECT_EQ(1U, makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 0)->weight());
  EXPECT_EQ(128U, makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 128)->weight());
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", std::numeric_limits<uint32_t>::max())
                ->weight());

  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 50);
  EXPECT_EQ(50U, host->weight());
  host->weight(51);
  EXPECT_EQ(51U, host->weight());
  host->weight(0);
  EXPECT_EQ(1U, host->weight());
  host->weight(std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), host->weight());
}

TEST(HostImplTest, HostnameCanaryAndLocality) {
  MockClusterMockPrioritySet cluster;
  envoy::config::core::v3::Metadata metadata;
  Config::Metadata::mutableMetadataValue(metadata, Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);
  envoy::config::core::v3::Locality locality;
  locality.set_region("oceania");
  locality.set_zone("hello");
  locality.set_sub_zone("world");
  HostImpl host(cluster.info_, "lyft.com", Network::Utility::resolveUrl("tcp://10.0.0.1:1234"),
                std::make_shared<const envoy::config::core::v3::Metadata>(metadata), 1, locality,
                envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 1,
                envoy::config::core::v3::UNKNOWN);
  EXPECT_EQ(cluster.info_.get(), &host.cluster());
  EXPECT_EQ("lyft.com", host.hostname());
  EXPECT_TRUE(host.canary());
  EXPECT_EQ("oceania", host.locality().region());
  EXPECT_EQ("hello", host.locality().zone());
  EXPECT_EQ("world", host.locality().sub_zone());
  EXPECT_EQ(1, host.priority());
}

TEST(HostImplTest, HealthFlags) {
  MockClusterMockPrioritySet cluster;
  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 1);

  // To begin with, no flags are set so we're healthy.
  EXPECT_EQ(Host::Health::Healthy, host->health());

  // Setting an unhealthy flag make the host unhealthy.
  host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Unhealthy, host->health());

  // Setting a degraded flag on an unhealthy host has no effect.
  host->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Unhealthy, host->health());

  // If the degraded flag is the only thing set, host is degraded.
  host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Degraded, host->health());

  // If the EDS and active degraded flag is set, host is degraded.
  host->healthFlagSet(Host::HealthFlag::DEGRADED_EDS_HEALTH);
  EXPECT_EQ(Host::Health::Degraded, host->health());

  // If only the EDS degraded is set, host is degraded.
  host->healthFlagClear(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Degraded, host->health());

  // If EDS and failed active hc is set, host is unhealthy.
  host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Unhealthy, host->health());
}

// Test that it's not possible to do a HostDescriptionImpl with a unix
// domain socket host and a health check config with non-zero port.
// This is a regression test for oss-fuzz issue
// https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=11095
TEST(HostImplTest, HealthPipeAddress) {
  EXPECT_THROW_WITH_MESSAGE(
      {
        std::shared_ptr<MockClusterInfo> info{new NiceMock<MockClusterInfo>()};
        envoy::config::endpoint::v3::Endpoint::HealthCheckConfig config;
        config.set_port_value(8000);
        HostDescriptionImpl descr(info, "", Network::Utility::resolveUrl("unix://foo"), nullptr,
                                  envoy::config::core::v3::Locality().default_instance(), config,
                                  1);
      },
      EnvoyException, "Invalid host configuration: non-zero port for non-IP address");
}

// Test that hostname flag from the health check config propagates.
TEST(HostImplTest, HealthcheckHostname) {
  std::shared_ptr<MockClusterInfo> info{new NiceMock<MockClusterInfo>()};
  envoy::config::endpoint::v3::Endpoint::HealthCheckConfig config;
  config.set_hostname("foo");
  HostDescriptionImpl descr(info, "", Network::Utility::resolveUrl("tcp://1.2.3.4:80"), nullptr,
                            envoy::config::core::v3::Locality().default_instance(), config, 1);
  EXPECT_EQ("foo", descr.hostnameForHealthChecks());
}

class StaticClusterImplTest : public testing::Test, public UpstreamImplTestBase {};

TEST_F(StaticClusterImplTest, InitialHosts) {
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
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST_F(StaticClusterImplTest, LoadAssignmentEmptyHostname) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      policy:
        overprovisioning_factor: 100
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

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_EQ(100, cluster.prioritySet().hostSetsPerPriority()[0]->overprovisioningFactor());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST_F(StaticClusterImplTest, LoadAssignmentNonEmptyHostname) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      endpoints:
      - lb_endpoints:
        - endpoint:
            hostname: foo
            address:
              socket_address:
                address: 10.0.0.1
                port_value: 443
            health_check_config:
              port_value: 8000
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("foo", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST_F(StaticClusterImplTest, LoadAssignmentNonEmptyHostnameWithHealthChecks) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      endpoints:
      - lb_endpoints:
        - endpoint:
            hostname: foo
            address:
              socket_address:
                address: 10.0.0.1
                port_value: 443
            health_check_config:
              port_value: 8000
              hostname: "foo2"
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("foo", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_EQ("foo2",
            cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostnameForHealthChecks());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST_F(StaticClusterImplTest, LoadAssignmentMultiplePriorities) {
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

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[1]->healthyHosts().size());
  EXPECT_EQ("", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_FALSE(cluster.info()->addedViaApi());
}

TEST_F(StaticClusterImplTest, LoadAssignmentLocality) {
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

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
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

// Validates that setting an EDS health value through LoadAssignment is honored for static
// clusters.
TEST_F(StaticClusterImplTest, LoadAssignmentEdsHealth) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      policy:
        overprovisioning_factor: 100
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 10.0.0.1
                port_value: 443
            health_check_config:
              port_value: 8000
          health_status: DEGRADED
  )EOF";

  NiceMock<MockClusterManager> cm;
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(Host::Health::Degraded,
            cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->health());
}

TEST_F(StaticClusterImplTest, AltStatName) {
  const std::string yaml = R"EOF(
    name: staticcluster
    alt_stat_name: staticcluster_stats
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 443
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
  cluster.initialize([] {});
  // Increment a stat and verify it is emitted with alt_stat_name
  cluster.info()->stats().upstream_rq_total_.inc();
  EXPECT_EQ(1UL, stats_.counter("cluster.staticcluster_stats.upstream_rq_total").value());
}

TEST_F(StaticClusterImplTest, RingHash) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: static
    lb_policy: ring_hash
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11001
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), true);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(LoadBalancerType::RingHash, cluster.info()->lbType());
  EXPECT_TRUE(cluster.info()->addedViaApi());
}

TEST_F(StaticClusterImplTest, OutlierDetector) {
  const std::string yaml = R"EOF(
    name: addressportconfig
    connect_timeout: 0.25s
    type: static
    lb_policy: random
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11001                  
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11002
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);

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

TEST_F(StaticClusterImplTest, HealthyStat) {
  const std::string yaml = R"EOF(
    name: addressportconfig
    connect_timeout: 0.25s
    type: static
    lb_policy: random
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11001                  
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11002
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);

  Outlier::MockDetector* outlier_detector = new NiceMock<Outlier::MockDetector>();
  cluster.setOutlierDetector(Outlier::DetectorSharedPtr{outlier_detector});

  std::shared_ptr<MockHealthChecker> health_checker(new NiceMock<MockHealthChecker>());
  cluster.setHealthChecker(health_checker);

  ReadyWatcher initialized;
  cluster.initialize([&initialized] { initialized.ready(); });

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_degraded_.value());

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
  EXPECT_EQ(0UL, cluster.info()->stats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                               HealthTransition::Changed);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                               HealthTransition::Changed);
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagSet(
      Host::HealthFlag::DEGRADED_ACTIVE_HC);
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_degraded_.value());

  // Mark the endpoint as unhealthy. This should decrement the degraded stat.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_degraded_.value());

  // Go back to degraded.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_degraded_.value());

  // Then go healthy.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagClear(
      Host::HealthFlag::DEGRADED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(1UL, cluster.info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->stats().membership_degraded_.value());
}

TEST_F(StaticClusterImplTest, UrlConfig) {
  const std::string yaml = R"EOF(
    name: addressportconfig
    connect_timeout: 0.25s
    type: static
    lb_policy: random
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11001                  
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.2
                    port_value: 11002
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
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
  EXPECT_EQ(::Envoy::Http2::Utility::OptionsLimits::DEFAULT_HPACK_TABLE_SIZE,
            cluster.info()->http2Options().hpack_table_size().value());
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

TEST_F(StaticClusterImplTest, UnsupportedLBType) {
  const std::string yaml = R"EOF(
    name: addressportconfig
    connect_timeout: 0.25s
    type: static
    lb_policy: fakelbtype
    load_assignment:
      cluster_name: addressportconfig
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address: { address: 192.168.1.1, port_value: 22 }
              socket_address: { address: 192.168.1.2, port_value: 44 }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      {
        envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
        Envoy::Stats::ScopePtr scope =
            stats_.createScope(fmt::format("cluster.{}.", cluster_config.alt_stat_name().empty()
                                                              ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
        Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
            admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
            singleton_manager_, tls_, validation_visitor_, *api_);
        StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope),
                                  false);
      },
      EnvoyException,
      "Protobuf message (type envoy.config.cluster.v3.Cluster reason "
      "INVALID_ARGUMENT:(lb_policy): invalid "
      "value \"fakelbtype\" for type TYPE_ENUM) has unknown fields");
}

TEST_F(StaticClusterImplTest, MalformedHostIP) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  EXPECT_THROW_WITH_MESSAGE(
      StaticClusterImpl(cluster_config, runtime_, factory_context, std::move(scope), false),
      EnvoyException,
      "malformed IP address: foo.bar.com. Consider setting resolver_name or "
      "setting cluster type to 'STRICT_DNS' or 'LOGICAL_DNS'");
}

// Test for oss-fuzz issue #11329
// (https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=11329). If no
// hosts were specified in endpoints but a priority value > 0 there, a
// crash would happen.
TEST_F(StaticClusterImplTest, NoHostsTest) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    load_assignment:
      cluster_name: foo
      endpoints:
      - priority: 1
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope =
      stats_.createScope(fmt::format("cluster.{}.", cluster_config.name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);
  StaticClusterImpl cluster(cluster_config, runtime_, factory_context, std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

TEST_F(StaticClusterImplTest, SourceAddressPriority) {
  envoy::config::cluster::v3::Cluster config;
  config.set_name("staticcluster");
  config.mutable_connect_timeout();

  {
    // If the cluster manager gets a source address from the bootstrap proto, use it.
    cm_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    StaticClusterImpl cluster(config, runtime_, factory_context, std::move(scope), false);
    EXPECT_EQ("1.2.3.5:0", cluster.info()->sourceAddress()->asString());
  }

  const std::string cluster_address = "5.6.7.8";
  config.mutable_upstream_bind_config()->mutable_source_address()->set_address(cluster_address);
  {
    // Verify source address from cluster config is used when present.
    Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    StaticClusterImpl cluster(config, runtime_, factory_context, std::move(scope), false);
    EXPECT_EQ(cluster_address, cluster.info()->sourceAddress()->ip()->addressAsString());
  }

  {
    // The source address from cluster config takes precedence over one from the bootstrap proto.
    cm_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    StaticClusterImpl cluster(config, runtime_, factory_context, std::move(scope), false);
    EXPECT_EQ(cluster_address, cluster.info()->sourceAddress()->ip()->addressAsString());
  }
}

class ClusterImplTest : public testing::Test, public UpstreamImplTestBase {};

// Test that the correct feature() is set when close_connections_on_host_health_failure is
// configured.
TEST_F(ClusterImplTest, CloseConnectionsOnHostHealthFailure) {
  auto dns_resolver = std::make_shared<Network::MockDnsResolver>();
  ReadyWatcher initialized;

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    close_connections_on_host_health_failure: true
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
  )EOF";
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopePtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      admin_, ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_,
      singleton_manager_, tls_, validation_visitor_, *api_);

  StrictDnsClusterImpl cluster(cluster_config, runtime_, dns_resolver, factory_context,
                               std::move(scope), false);
  EXPECT_TRUE(cluster.info()->features() &
              ClusterInfo::Features::CLOSE_CONNECTIONS_ON_HOST_HEALTH_FAILURE);
}

class TestBatchUpdateCb : public PrioritySet::BatchUpdateCb {
public:
  TestBatchUpdateCb(HostVectorSharedPtr hosts, HostsPerLocalitySharedPtr hosts_per_locality)
      : hosts_(hosts), hosts_per_locality_(hosts_per_locality) {}

  void batchUpdate(PrioritySet::HostUpdateCb& host_update_cb) override {
    // Add the host from P1 to P0.
    {
      HostVector hosts_added{hosts_->front()};
      HostVector hosts_removed{};
      host_update_cb.updateHosts(
          0,
          updateHostsParams(hosts_, hosts_per_locality_,
                            std::make_shared<const HealthyHostVector>(*hosts_),
                            hosts_per_locality_),
          {}, hosts_added, hosts_removed, absl::nullopt);
    }

    // Remove the host from P1.
    {
      HostVectorSharedPtr empty_hosts = std::make_shared<HostVector>();
      HostVector hosts_added{};
      HostVector hosts_removed{hosts_->front()};
      host_update_cb.updateHosts(
          1,
          updateHostsParams(empty_hosts, HostsPerLocalityImpl::empty(),
                            std::make_shared<const HealthyHostVector>(*empty_hosts),
                            HostsPerLocalityImpl::empty()),
          {}, hosts_added, hosts_removed, absl::nullopt);
    }
  }

  HostVectorSharedPtr hosts_;
  HostsPerLocalitySharedPtr hosts_per_locality_;
};

// Test creating and extending a priority set.
TEST(PrioritySet, Extend) {
  PrioritySetImpl priority_set;
  priority_set.getOrCreateHostSet(0);

  uint32_t priority_changes = 0;
  uint32_t membership_changes = 0;
  uint32_t last_priority = 0;
  priority_set.addPriorityUpdateCb(
      [&](uint32_t priority, const HostVector&, const HostVector&) -> void {
        last_priority = priority;
        ++priority_changes;
      });
  priority_set.addMemberUpdateCb(
      [&](const HostVector&, const HostVector&) -> void { ++membership_changes; });

  // The initial priority set starts with priority level 0..
  EXPECT_EQ(1, priority_set.hostSetsPerPriority().size());
  EXPECT_EQ(0, priority_set.hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0, priority_set.hostSetsPerPriority()[0]->priority());

  // Add priorities 1 and 2, ensure the callback is called, and that the new
  // host sets are created with the correct priority.
  EXPECT_EQ(0, priority_changes);
  EXPECT_EQ(0, membership_changes);
  EXPECT_EQ(0, priority_set.getOrCreateHostSet(2).hosts().size());
  EXPECT_EQ(3, priority_set.hostSetsPerPriority().size());
  // No-op host set creation does not trigger callbacks.
  EXPECT_EQ(0, priority_changes);
  EXPECT_EQ(0, membership_changes);
  EXPECT_EQ(last_priority, 0);
  EXPECT_EQ(1, priority_set.hostSetsPerPriority()[1]->priority());
  EXPECT_EQ(2, priority_set.hostSetsPerPriority()[2]->priority());

  // Now add hosts for priority 1, and ensure they're added and subscribers are notified.
  std::shared_ptr<MockClusterInfo> info{new NiceMock<MockClusterInfo>()};
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info, "tcp://127.0.0.1:80")}));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  {
    HostVector hosts_added{hosts->front()};
    HostVector hosts_removed{};

    priority_set.updateHosts(1,
                             updateHostsParams(hosts, hosts_per_locality,
                                               std::make_shared<const HealthyHostVector>(*hosts),
                                               hosts_per_locality),
                             {}, hosts_added, hosts_removed, absl::nullopt);
  }
  EXPECT_EQ(1, priority_changes);
  EXPECT_EQ(1, membership_changes);
  EXPECT_EQ(last_priority, 1);
  EXPECT_EQ(1, priority_set.hostSetsPerPriority()[1]->hosts().size());

  // Test iteration.
  int i = 0;
  for (auto& host_set : priority_set.hostSetsPerPriority()) {
    EXPECT_EQ(host_set.get(), priority_set.hostSetsPerPriority()[i++].get());
  }

  // Test batch host updates. Verify that we can move a host without triggering intermediate host
  // updates.

  // We're going to do a noop host change, so add a callback to assert that we're not announcing
  // any host changes.
  priority_set.addMemberUpdateCb([&](const HostVector& added, const HostVector& removed) -> void {
    EXPECT_TRUE(added.empty() && removed.empty());
  });

  TestBatchUpdateCb batch_update(hosts, hosts_per_locality);
  priority_set.batchHostUpdate(batch_update);

  // We expect to see two priority changes, but only one membership change.
  EXPECT_EQ(3, priority_changes);
  EXPECT_EQ(2, membership_changes);
}

class ClusterInfoImplTest : public testing::Test {
public:
  ClusterInfoImplTest() : api_(Api::createApiForTest(stats_)) {}

  std::unique_ptr<StrictDnsClusterImpl> makeCluster(const std::string& yaml,
                                                    bool avoid_boosting = true) {
    cluster_config_ = parseClusterFromV3Yaml(yaml, avoid_boosting);
    scope_ = stats_.createScope(fmt::format("cluster.{}.", cluster_config_.alt_stat_name().empty()
                                                               ? cluster_config_.name()
                                                               : cluster_config_.alt_stat_name()));
    factory_context_ = std::make_unique<Server::Configuration::TransportSocketFactoryContextImpl>(
        admin_, ssl_context_manager_, *scope_, cm_, local_info_, dispatcher_, random_, stats_,
        singleton_manager_, tls_, validation_visitor_, *api_);

    return std::make_unique<StrictDnsClusterImpl>(cluster_config_, runtime_, dns_resolver_,
                                                  *factory_context_, std::move(scope_), false);
  }

  Stats::TestUtil::TestStore stats_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<Network::MockDnsResolver> dns_resolver_{new NiceMock<Network::MockDnsResolver>()};
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<MockClusterManager> cm_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  ReadyWatcher initialized_;
  envoy::config::cluster::v3::Cluster cluster_config_;
  Envoy::Stats::ScopePtr scope_;
  std::unique_ptr<Server::Configuration::TransportSocketFactoryContextImpl> factory_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
};

struct Foo : public Envoy::Config::TypedMetadata::Object {};

struct Baz : public Envoy::Config::TypedMetadata::Object {
  Baz(std::string n) : name(n) {}
  std::string name;
};

class BazFactory : public ClusterTypedMetadataFactory {
public:
  std::string name() const override { return "baz"; }
  // Returns nullptr (conversion failure) if d is empty.
  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const ProtobufWkt::Struct& d) const override {
    if (d.fields().find("name") != d.fields().end()) {
      return std::make_unique<Baz>(d.fields().at("name").string_value());
    }
    throw EnvoyException("Cannot create a Baz when metadata is empty.");
  }
};

// Cluster metadata and common config retrieval.
TEST_F(ClusterInfoImplTest, Metadata) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: MAGLEV
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    metadata: { filter_metadata: { com.bar.foo: { baz: test_value },
                                   baz: {name: meh } } }
    common_lb_config:
      healthy_panic_threshold:
        value: 0.3
  )EOF";

  BazFactory baz_factory;
  Registry::InjectFactory<ClusterTypedMetadataFactory> registered_factory(baz_factory);
  auto cluster = makeCluster(yaml);

  EXPECT_EQ("meh", cluster->info()->typedMetadata().get<Baz>(baz_factory.name())->name);
  EXPECT_EQ(nullptr, cluster->info()->typedMetadata().get<Foo>(baz_factory.name()));
  EXPECT_EQ("test_value",
            Config::Metadata::metadataValue(&cluster->info()->metadata(), "com.bar.foo", "baz")
                .string_value());
  EXPECT_EQ(0.3, cluster->info()->lbConfig().healthy_panic_threshold().value());
  EXPECT_EQ(LoadBalancerType::Maglev, cluster->info()->lbType());
}

// Eds service_name is populated.
TEST_F(ClusterInfoImplTest, EdsServiceNamePopulation) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: EDS
    lb_policy: MAGLEV
    eds_cluster_config:
      service_name: service_foo
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    common_lb_config:
      healthy_panic_threshold:
        value: 0.3
  )EOF";
  auto cluster = makeCluster(yaml);
  EXPECT_EQ(cluster->info()->edsServiceName(), "service_foo");

  const std::string unexpected_eds_config_yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: MAGLEV
    eds_cluster_config:
      service_name: service_foo
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    common_lb_config:
      healthy_panic_threshold:
        value: 0.3
  )EOF";
  EXPECT_THROW_WITH_MESSAGE(makeCluster(unexpected_eds_config_yaml), EnvoyException,
                            "eds_cluster_config set in a non-EDS cluster");
}

// Typed metadata loading throws exception.
TEST_F(ClusterInfoImplTest, BrokenTypedMetadata) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: MAGLEV
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    metadata: { filter_metadata: { com.bar.foo: { baz: test_value },
                                   baz: {boom: meh} } }
    common_lb_config:
      healthy_panic_threshold:
        value: 0.3
  )EOF";

  BazFactory baz_factory;
  Registry::InjectFactory<ClusterTypedMetadataFactory> registered_factory(baz_factory);
  EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml), EnvoyException,
                            "Cannot create a Baz when metadata is empty.");
}

// Cluster extension protocol options fails validation when configured for an unregistered filter.
TEST_F(ClusterInfoImplTest, ExtensionProtocolOptionsForUnknownFilter) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    typed_extension_protocol_options:
      no_such_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value:
          option: "value"
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml, false), EnvoyException,
                            "Didn't find a registered network or http filter implementation for "
                            "name: 'no_such_filter'");
}

TEST_F(ClusterInfoImplTest, TypedExtensionProtocolOptionsForUnknownFilter) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    typed_extension_protocol_options:
      no_such_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml), EnvoyException,
                            "Didn't find a registered network or http filter implementation for "
                            "name: 'no_such_filter'");
}

// This test case can't be converted for V3 API as it is specific for extension_protocol_options
TEST_F(ClusterInfoImplTest, OneofExtensionProtocolOptionsForUnknownFilter) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    hosts: [{ socket_address: { address: foo.bar.com, port_value: 443 }}]
    extension_protocol_options:
      no_such_filter: { option: value }
    typed_extension_protocol_options:
      no_such_filter:
        "@type": type.googleapis.com/google.protobuf.Struct
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml, false), EnvoyException,
                            "Only one of typed_extension_protocol_options or "
                            "extension_protocol_options can be specified");
}

TEST_F(ClusterInfoImplTest, TestTrackRequestResponseSizesNotSetInConfig) {
  const std::string yaml_disabled = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
  )EOF";

  auto cluster = makeCluster(yaml_disabled);
  // By default, histograms tracking request/response sizes are not published.
  EXPECT_FALSE(cluster->info()->requestResponseSizeStats().has_value());

  const std::string yaml_disabled2 = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    track_cluster_stats: { timeout_budgets : true }
  )EOF";

  cluster = makeCluster(yaml_disabled2);
  EXPECT_FALSE(cluster->info()->requestResponseSizeStats().has_value());

  const std::string yaml_disabled3 = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    track_cluster_stats: { request_response_sizes : false }
  )EOF";

  cluster = makeCluster(yaml_disabled3);
  EXPECT_FALSE(cluster->info()->requestResponseSizeStats().has_value());
}

TEST_F(ClusterInfoImplTest, TestTrackRequestResponseSizes) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    track_cluster_stats: { request_response_sizes : true }
  )EOF";

  auto cluster = makeCluster(yaml);
  // The stats should be created.
  ASSERT_TRUE(cluster->info()->requestResponseSizeStats().has_value());

  Upstream::ClusterRequestResponseSizeStats req_resp_stats =
      cluster->info()->requestResponseSizeStats()->get();

  EXPECT_EQ(Stats::Histogram::Unit::Bytes, req_resp_stats.upstream_rq_headers_size_.unit());
  EXPECT_EQ(Stats::Histogram::Unit::Bytes, req_resp_stats.upstream_rq_body_size_.unit());
  EXPECT_EQ(Stats::Histogram::Unit::Bytes, req_resp_stats.upstream_rs_body_size_.unit());
}

TEST_F(ClusterInfoImplTest, TestTrackRemainingResourcesGauges) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN

    circuit_breakers:
      thresholds:
      - priority: DEFAULT
        max_connections: 1
        max_pending_requests: 2
        max_requests: 3
        max_retries: 4
        track_remaining: false
      - priority: HIGH
        max_connections: 1
        max_pending_requests: 2
        max_requests: 3
        max_retries: 4
        track_remaining: true
  )EOF";

  auto cluster = makeCluster(yaml);

  // The value of a remaining resource gauge will always be 0 for the default
  // priority circuit breaker since track_remaining is false
  Stats::Gauge& default_remaining_retries =
      stats_.gauge("cluster.name.circuit_breakers.default.remaining_retries",
                   Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(0U, default_remaining_retries.value());
  cluster->info()->resourceManager(ResourcePriority::Default).retries().inc();
  EXPECT_EQ(0U, default_remaining_retries.value());
  cluster->info()->resourceManager(ResourcePriority::Default).retries().dec();
  EXPECT_EQ(0U, default_remaining_retries.value());

  // This gauge will be correctly set since we have opted in to tracking remaining
  // resource gauges in the high priority circuit breaker.
  Stats::Gauge& high_remaining_retries = stats_.gauge(
      "cluster.name.circuit_breakers.high.remaining_retries", Stats::Gauge::ImportMode::Accumulate);
  EXPECT_EQ(4U, high_remaining_retries.value());
  cluster->info()->resourceManager(ResourcePriority::High).retries().inc();
  EXPECT_EQ(3U, high_remaining_retries.value());
  cluster->info()->resourceManager(ResourcePriority::High).retries().dec();
  EXPECT_EQ(4U, high_remaining_retries.value());
}

TEST_F(ClusterInfoImplTest, Timeouts) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: MAGLEV
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    metadata: { filter_metadata: { com.bar.foo: { baz: test_value },
                                   baz: {name: meh } } }
    common_lb_config:
      healthy_panic_threshold:
        value: 0.3
  )EOF";

  BazFactory baz_factory;
  Registry::InjectFactory<ClusterTypedMetadataFactory> registered_factory(baz_factory);
  auto cluster1 = makeCluster(yaml);
  ASSERT_TRUE(cluster1->info()->idleTimeout().has_value());
  EXPECT_EQ(std::chrono::hours(1), cluster1->info()->idleTimeout().value());

  const std::string explicit_timeout = R"EOF(
    common_http_protocol_options:
      idle_timeout: 1s
  )EOF";

  auto cluster2 = makeCluster(yaml + explicit_timeout);
  ASSERT_TRUE(cluster2->info()->idleTimeout().has_value());
  EXPECT_EQ(std::chrono::seconds(1), cluster2->info()->idleTimeout().value());

  const std::string no_timeout = R"EOF(
    common_http_protocol_options:
      idle_timeout: 0s
  )EOF";
  auto cluster3 = makeCluster(yaml + no_timeout);
  EXPECT_FALSE(cluster3->info()->idleTimeout().has_value());
}

TEST_F(ClusterInfoImplTest, TestTrackTimeoutBudgetsNotSetInConfig) {
  // Check that without the flag specified, the histogram is null.
  const std::string yaml_disabled = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
  )EOF";

  auto cluster = makeCluster(yaml_disabled);
  // The stats will be null if they have not been explicitly turned on.
  EXPECT_FALSE(cluster->info()->timeoutBudgetStats().has_value());

  const std::string yaml_disabled2 = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    track_cluster_stats: { request_response_sizes : true }
  )EOF";

  cluster = makeCluster(yaml_disabled2);
  EXPECT_FALSE(cluster->info()->timeoutBudgetStats().has_value());

  const std::string yaml_disabled3 = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    track_cluster_stats: { timeout_budgets : false }
  )EOF";

  cluster = makeCluster(yaml_disabled3);
  EXPECT_FALSE(cluster->info()->timeoutBudgetStats().has_value());
}

TEST_F(ClusterInfoImplTest, TestTrackTimeoutBudgets) {
  // Check that with the flag, the histogram is created.
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    track_cluster_stats: { timeout_budgets : true }
  )EOF";

  auto cluster = makeCluster(yaml);
  // The stats should be created.
  ASSERT_TRUE(cluster->info()->timeoutBudgetStats().has_value());

  Upstream::ClusterTimeoutBudgetStats tb_stats = cluster->info()->timeoutBudgetStats()->get();
  EXPECT_EQ(Stats::Histogram::Unit::Unspecified,
            tb_stats.upstream_rq_timeout_budget_percent_used_.unit());
  EXPECT_EQ(Stats::Histogram::Unit::Unspecified,
            tb_stats.upstream_rq_timeout_budget_per_try_percent_used_.unit());
}

TEST_F(ClusterInfoImplTest, DEPRECATED_FEATURE_TEST(TestTrackTimeoutBudgetsOld)) {
  // Check that without the flag specified, the histogram is null.
  const std::string yaml_disabled = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
  )EOF";

  auto cluster = makeCluster(yaml_disabled);
  // The stats will be null if they have not been explicitly turned on.
  EXPECT_FALSE(cluster->info()->timeoutBudgetStats().has_value());

  // Check that with the flag, the histogram is created.
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    track_timeout_budgets: true
  )EOF";

  cluster = makeCluster(yaml);
  // The stats should be created.
  ASSERT_TRUE(cluster->info()->timeoutBudgetStats().has_value());

  Upstream::ClusterTimeoutBudgetStats tb_stats = cluster->info()->timeoutBudgetStats()->get();
  EXPECT_EQ(Stats::Histogram::Unit::Unspecified,
            tb_stats.upstream_rq_timeout_budget_percent_used_.unit());
  EXPECT_EQ(Stats::Histogram::Unit::Unspecified,
            tb_stats.upstream_rq_timeout_budget_per_try_percent_used_.unit());
}

// Validates HTTP2 SETTINGS config.
TEST_F(ClusterInfoImplTest, Http2ProtocolOptions) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    http2_protocol_options:
      hpack_table_size: 2048
      initial_stream_window_size: 65536
      custom_settings_parameters:
        - identifier: 0x10
          value: 10
        - identifier: 0x12
          value: 12
  )EOF";

  auto cluster = makeCluster(yaml);
  EXPECT_EQ(cluster->info()->http2Options().hpack_table_size().value(), 2048);
  EXPECT_EQ(cluster->info()->http2Options().initial_stream_window_size().value(), 65536);
  EXPECT_EQ(cluster->info()->http2Options().custom_settings_parameters()[0].identifier().value(),
            0x10);
  EXPECT_EQ(cluster->info()->http2Options().custom_settings_parameters()[0].value().value(), 10);
  EXPECT_EQ(cluster->info()->http2Options().custom_settings_parameters()[1].identifier().value(),
            0x12);
  EXPECT_EQ(cluster->info()->http2Options().custom_settings_parameters()[1].value().value(), 12);
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
  createProtocolOptionsConfig(const Protobuf::Message& msg,
                              ProtobufMessage::ValidationVisitor&) override {
    return parent_.createProtocolOptionsConfig(msg);
  }
  std::string name() const override { CONSTRUCT_ON_FIRST_USE(std::string, "envoy.test.filter"); }
  std::string configType() override { return ""; };

  TestFilterConfigFactoryBase& parent_;
};

class TestHttpFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  TestHttpFilterConfigFactory(TestFilterConfigFactoryBase& parent) : parent_(parent) {}

  // NamedNetworkFilterConfigFactory
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
                                  Server::Configuration::ServerFactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return parent_.createEmptyProtocolOptionsProto();
  }
  Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsConfig(const Protobuf::Message& msg,
                              ProtobufMessage::ValidationVisitor&) override {
    return parent_.createProtocolOptionsConfig(msg);
  }
  std::string name() const override { CONSTRUCT_ON_FIRST_USE(std::string, "envoy.test.filter"); }
  std::string configType() override { return ""; };

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
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    typed_extension_protocol_options:
      envoy.test.filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value:
          option: "value"
  )EOF";

  {
    TestNetworkFilterConfigFactory factory(factoryBase);
    Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> registry(
        factory);
    EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml, false), EnvoyException,
                              "filter envoy.test.filter does not support protocol options");
  }
  {
    TestHttpFilterConfigFactory factory(factoryBase);
    Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registry(factory);
    EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml, false), EnvoyException,
                              "filter envoy.test.filter does not support protocol options");
  }
}

TEST_F(ClusterInfoImplTest, TypedExtensionProtocolOptionsForFilterWithoutOptions) {
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
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    typed_extension_protocol_options:
      envoy.test.filter: { "@type": type.googleapis.com/google.protobuf.Struct }
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
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    typed_extension_protocol_options:
      envoy.test.filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value:
          option: "value"
  )EOF";

  const std::string typed_yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
    typed_extension_protocol_options:
      envoy.test.filter:
        "@type": type.googleapis.com/google.protobuf.Struct
        value:
          option: "value"
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
  {
    // Get the cluster with extension_protocol_options for a network filter factory.
    TestNetworkFilterConfigFactory factory(factoryBase);
    Registry::InjectFactory<Server::Configuration::NamedNetworkFilterConfigFactory> registry(
        factory);
    clusters.push_back(makeCluster(typed_yaml));
  }
  {
    // Get the cluster with extension_protocol_options for an http filter factory.
    TestHttpFilterConfigFactory factory(factoryBase);
    Registry::InjectFactory<Server::Configuration::NamedHttpFilterConfigFactory> registry(factory);
    clusters.push_back(makeCluster(typed_yaml));
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

TEST_F(ClusterInfoImplTest, UseDownstreamHttpProtocol) {
  const std::string yaml = R"EOF(
  name: name
  connect_timeout: 0.25s
  type: STRICT_DNS
  lb_policy: ROUND_ROBIN
  protocol_selection: USE_DOWNSTREAM_PROTOCOL
)EOF";

  auto cluster = makeCluster(yaml);

  EXPECT_EQ(Http::Protocol::Http10,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http10}));
  EXPECT_EQ(Http::Protocol::Http11,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http11}));
  EXPECT_EQ(Http::Protocol::Http2, cluster->info()->upstreamHttpProtocol({Http::Protocol::Http2}));
}

TEST_F(ClusterInfoImplTest, UpstreamHttp2Protocol) {
  const std::string yaml = R"EOF(
  name: name
  connect_timeout: 0.25s
  type: STRICT_DNS
  lb_policy: ROUND_ROBIN
  http2_protocol_options: {}
)EOF";

  auto cluster = makeCluster(yaml);

  EXPECT_EQ(Http::Protocol::Http2, cluster->info()->upstreamHttpProtocol(absl::nullopt));
  EXPECT_EQ(Http::Protocol::Http2, cluster->info()->upstreamHttpProtocol({Http::Protocol::Http10}));
  EXPECT_EQ(Http::Protocol::Http2, cluster->info()->upstreamHttpProtocol({Http::Protocol::Http11}));
  EXPECT_EQ(Http::Protocol::Http2, cluster->info()->upstreamHttpProtocol({Http::Protocol::Http2}));
}

TEST_F(ClusterInfoImplTest, UpstreamHttp11Protocol) {
  const std::string yaml = R"EOF(
  name: name
  connect_timeout: 0.25s
  type: STRICT_DNS
  lb_policy: ROUND_ROBIN
)EOF";

  auto cluster = makeCluster(yaml);

  EXPECT_EQ(Http::Protocol::Http11, cluster->info()->upstreamHttpProtocol(absl::nullopt));
  EXPECT_EQ(Http::Protocol::Http11,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http10}));
  EXPECT_EQ(Http::Protocol::Http11,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http11}));
  EXPECT_EQ(Http::Protocol::Http11, cluster->info()->upstreamHttpProtocol({Http::Protocol::Http2}));
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

  MockClusterMockPrioritySet cluster;
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
  MockClusterMockPrioritySet cluster;
  HostSharedPtr host_0 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 1);
  HostSharedPtr host_1 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", 1);

  {
    std::vector<HostVector> locality_hosts = {{host_0}, {host_1}};
    const auto filtered =
        HostsPerLocalityImpl(std::move(locality_hosts), false).filter({[&host_0](const Host& host) {
          return &host == host_0.get();
        }})[0];
    EXPECT_FALSE(filtered->hasLocalLocality());
    const std::vector<HostVector> expected_locality_hosts = {{host_0}, {}};
    EXPECT_EQ(expected_locality_hosts, filtered->get());
  }

  {
    std::vector<HostVector> locality_hosts = {{host_0}, {host_1}};
    auto filtered =
        HostsPerLocalityImpl(std::move(locality_hosts), true).filter({[&host_1](const Host& host) {
          return &host == host_1.get();
        }})[0];
    EXPECT_TRUE(filtered->hasLocalLocality());
    const std::vector<HostVector> expected_locality_hosts = {{}, {host_1}};
    EXPECT_EQ(expected_locality_hosts, filtered->get());
  }
}

class HostSetImplLocalityTest : public testing::Test {
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
  EXPECT_FALSE(host_set_.chooseHealthyLocality().has_value());
}

// When no hosts are healthy we should fail to select a locality
TEST_F(HostSetImplLocalityTest, AllUnhealthy) {
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}, {hosts_[2]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1, 1}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(updateHostsParams(hosts, hosts_per_locality), locality_weights, {}, {},
                        absl::nullopt);
  EXPECT_FALSE(host_set_.chooseHealthyLocality().has_value());
}

// When a locality has endpoints that have not yet been warmed, weight calculation should ignore
// these hosts.
TEST_F(HostSetImplLocalityTest, NotWarmedHostsLocality) {
  // We have two localities with 3 hosts in L1, 2 hosts in L2. Two of the hosts in L1 are not
  // warmed yet, so even though they are unhealthy we should not adjust the locality weight.
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts_[0], hosts_[1], hosts_[2]}, {hosts_[3], hosts_[4]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  HostsPerLocalitySharedPtr healthy_hosts_per_locality =
      makeHostsPerLocality({{hosts_[0]}, {hosts_[3], hosts_[4]}});
  HostsPerLocalitySharedPtr excluded_hosts_per_locality =
      makeHostsPerLocality({{hosts_[1], hosts_[2]}, {}});

  host_set_.updateHosts(
      HostSetImpl::updateHostsParams(
          hosts, hosts_per_locality,
          makeHostsFromHostsPerLocality<HealthyHostVector>(healthy_hosts_per_locality),
          healthy_hosts_per_locality, std::make_shared<const DegradedHostVector>(),
          HostsPerLocalityImpl::empty(),
          makeHostsFromHostsPerLocality<ExcludedHostVector>(excluded_hosts_per_locality),
          excluded_hosts_per_locality),
      locality_weights, {}, {}, absl::nullopt);
  // We should RR between localities with equal weight.
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(1, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(1, host_set_.chooseHealthyLocality().value());
}

// When a locality has zero hosts, it should be treated as if it has zero healthy.
TEST_F(HostSetImplLocalityTest, EmptyLocality) {
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts_[0], hosts_[1], hosts_[2]}, {}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(updateHostsParams(hosts, hosts_per_locality,
                                          std::make_shared<const HealthyHostVector>(*hosts),
                                          hosts_per_locality),
                        locality_weights, {}, {}, absl::nullopt);
  // Verify that we are not RRing between localities.
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
}

// When all locality weights are zero we should fail to select a locality.
TEST_F(HostSetImplLocalityTest, AllZeroWeights) {
  HostsPerLocalitySharedPtr hosts_per_locality = makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{0, 0}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(updateHostsParams(hosts, hosts_per_locality,
                                          std::make_shared<const HealthyHostVector>(*hosts),
                                          hosts_per_locality),
                        locality_weights, {}, {});
  EXPECT_FALSE(host_set_.chooseHealthyLocality().has_value());
}

// When all locality weights are the same we have unweighted RR behavior.
TEST_F(HostSetImplLocalityTest, Unweighted) {
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}, {hosts_[2]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1, 1}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(updateHostsParams(hosts, hosts_per_locality,
                                          std::make_shared<const HealthyHostVector>(*hosts),
                                          hosts_per_locality),
                        locality_weights, {}, {}, absl::nullopt);
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(1, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(2, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(1, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(2, host_set_.chooseHealthyLocality().value());
}

// When locality weights differ, we have weighted RR behavior.
TEST_F(HostSetImplLocalityTest, Weighted) {
  HostsPerLocalitySharedPtr hosts_per_locality = makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 2}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(updateHostsParams(hosts, hosts_per_locality,
                                          std::make_shared<const HealthyHostVector>(*hosts),
                                          hosts_per_locality),
                        locality_weights, {}, {}, absl::nullopt);
  EXPECT_EQ(1, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(1, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(1, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(1, host_set_.chooseHealthyLocality().value());
}

// Localities with no weight assignment are never picked.
TEST_F(HostSetImplLocalityTest, MissingWeight) {
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts_[0]}, {hosts_[1]}, {hosts_[2]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 0, 1}};
  auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
  host_set_.updateHosts(updateHostsParams(hosts, hosts_per_locality,
                                          std::make_shared<const HealthyHostVector>(*hosts),
                                          hosts_per_locality),
                        locality_weights, {}, {}, absl::nullopt);
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(2, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(2, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(0, host_set_.chooseHealthyLocality().value());
  EXPECT_EQ(2, host_set_.chooseHealthyLocality().value());
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
    host_set_.updateHosts(updateHostsParams(hosts, hosts_per_locality,
                                            makeHostsFromHostsPerLocality<HealthyHostVector>(
                                                healthy_hosts_per_locality),
                                            healthy_hosts_per_locality),
                          locality_weights, {}, {}, absl::nullopt);
  };

  const auto expectPicks = [this](uint32_t locality_0_picks, uint32_t locality_1_picks) {
    uint32_t count[2] = {0, 0};
    for (uint32_t i = 0; i < 100; ++i) {
      const uint32_t locality_index = host_set_.chooseHealthyLocality().value();
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
    auto healthy_hosts =
        makeHostsFromHostsPerLocality<HealthyHostVector>(healthy_hosts_per_locality);
    host_set.updateHosts(updateHostsParams(std::make_shared<const HostVector>(hosts),
                                           hosts_per_locality, healthy_hosts,
                                           healthy_hosts_per_locality),
                         locality_weights, {}, {}, absl::nullopt);
    uint32_t cnts[] = {0, 0};
    for (uint32_t i = 0; i < 100; ++i) {
      absl::optional<uint32_t> locality_index = host_set.chooseHealthyLocality();
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

// Verifies that partitionHosts correctly splits hosts based on their health flags.
TEST(HostPartitionTest, PartitionHosts) {
  std::shared_ptr<MockClusterInfo> info{new NiceMock<MockClusterInfo>()};
  HostVector hosts{
      makeTestHost(info, "tcp://127.0.0.1:80"), makeTestHost(info, "tcp://127.0.0.1:81"),
      makeTestHost(info, "tcp://127.0.0.1:82"), makeTestHost(info, "tcp://127.0.0.1:83")};

  hosts[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  hosts[1]->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  hosts[2]->healthFlagSet(Host::HealthFlag::PENDING_ACTIVE_HC);
  hosts[2]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);

  auto hosts_per_locality = makeHostsPerLocality({{hosts[0], hosts[1]}, {hosts[2], hosts[3]}});

  auto update_hosts_params =
      HostSetImpl::partitionHosts(std::make_shared<const HostVector>(hosts), hosts_per_locality);

  EXPECT_EQ(4, update_hosts_params.hosts->size());
  EXPECT_EQ(1, update_hosts_params.healthy_hosts->get().size());
  EXPECT_EQ(hosts[3], update_hosts_params.healthy_hosts->get()[0]);
  EXPECT_EQ(1, update_hosts_params.degraded_hosts->get().size());
  EXPECT_EQ(hosts[1], update_hosts_params.degraded_hosts->get()[0]);
  EXPECT_EQ(1, update_hosts_params.excluded_hosts->get().size());
  EXPECT_EQ(hosts[2], update_hosts_params.excluded_hosts->get()[0]);

  EXPECT_EQ(2, update_hosts_params.hosts_per_locality->get()[0].size());
  EXPECT_EQ(2, update_hosts_params.hosts_per_locality->get()[1].size());

  EXPECT_EQ(0, update_hosts_params.healthy_hosts_per_locality->get()[0].size());
  EXPECT_EQ(1, update_hosts_params.healthy_hosts_per_locality->get()[1].size());
  EXPECT_EQ(hosts[3], update_hosts_params.healthy_hosts_per_locality->get()[1][0]);

  EXPECT_EQ(1, update_hosts_params.degraded_hosts_per_locality->get()[0].size());
  EXPECT_EQ(0, update_hosts_params.degraded_hosts_per_locality->get()[1].size());
  EXPECT_EQ(hosts[1], update_hosts_params.degraded_hosts_per_locality->get()[0][0]);

  EXPECT_EQ(0, update_hosts_params.excluded_hosts_per_locality->get()[0].size());
  EXPECT_EQ(1, update_hosts_params.excluded_hosts_per_locality->get()[1].size());
  EXPECT_EQ(hosts[2], update_hosts_params.excluded_hosts_per_locality->get()[1][0]);
}
} // namespace
} // namespace Upstream
} // namespace Envoy
