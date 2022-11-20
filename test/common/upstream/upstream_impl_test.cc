#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
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
#include "envoy/upstream/health_check_host_monitor.h"
#include "envoy/upstream/upstream.h"

#include "source/common/config/metadata.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/network/utility.h"
#include "source/common/singleton/manager_impl.h"
#include "source/extensions/clusters/static/static_cluster.h"
#include "source/extensions/clusters/strict_dns/strict_dns_cluster.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/options.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/mocks/upstream/health_checker.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/thread_aware_load_balancer.h"
#include "test/mocks/upstream/typed_load_balancer_factory.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::ContainerEq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {
namespace {

class UpstreamImplTestBase {
protected:
  UpstreamImplTestBase() : api_(Api::createApiForTest(stats_, random_)) {
    ON_CALL(server_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::TestUtil::TestStore stats_;
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);

  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver,
                               factory_context, std::move(scope), false);
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);
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

  ResolverData resolver(*dns_resolver_, server_context_.dispatcher_);
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);
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

TEST_F(StrictDnsClusterImplTest, DontWaitForDNSOnInit) {
  ResolverData resolver(*dns_resolver_, server_context_.dispatcher_);

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    dns_refresh_rate: 4s
    dns_failure_refresh_rate:
      base_interval: 7s
      max_interval: 10s
    wait_for_warm_on_init: false
    load_assignment:
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: foo.com
                port_value: 443
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);

  ReadyWatcher initialized;

  // Initialized without completing DNS resolution.
  EXPECT_CALL(initialized, ready());
  cluster.initialize([&]() -> void { initialized.ready(); });

  ReadyWatcher membership_updated;
  auto priority_update_cb = cluster.prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) -> void { membership_updated.ready(); });

  EXPECT_CALL(*resolver.timer_, enableTimer(std::chrono::milliseconds(4000), _));
  EXPECT_CALL(membership_updated, ready());
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                         TestUtility::makeDnsResponse({"127.0.0.2", "127.0.0.1"}));
}

TEST_F(StrictDnsClusterImplTest, Basic) {
  // gmock matches in LIFO order which is why these are swapped.
  ResolverData resolver2(*dns_resolver_, server_context_.dispatcher_);
  ResolverData resolver1(*dns_resolver_, server_context_.dispatcher_);

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
      per_host_thresholds:
      - priority: DEFAULT
        max_connections: 1
      - priority: HIGH
        max_connections: 990
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
                hostname: foo
                address:
                  socket_address:
                    address: localhost2
                    port_value: 11002
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);
  EXPECT_CALL(runtime_.snapshot_, getInteger("circuit_breakers.name.default.max_connections", 43))
      .Times(AnyNumber());
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
  EXPECT_EQ(1U, cluster.info()->resourceManager(ResourcePriority::Default).maxConnectionsPerHost());
  EXPECT_EQ(990U, cluster.info()->resourceManager(ResourcePriority::High).maxConnectionsPerHost());

  cluster.info()->trafficStats().upstream_rq_total_.inc();
  EXPECT_EQ(1UL, stats_.counter("cluster.name.upstream_rq_total").value());

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.maintenance_mode.name", 0));
  EXPECT_FALSE(cluster.info()->maintenanceMode());

  ReadyWatcher membership_updated;
  auto priority_update_cb = cluster.prioritySet().addPriorityUpdateCb(
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
  EXPECT_EQ("localhost1", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  EXPECT_EQ("foo", cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->hostname());

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

  EXPECT_CALL(resolver1.active_dns_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
  EXPECT_CALL(resolver2.active_dns_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));

  // Test per host connection limits: as it's set to 1, the host can create connections initially.
  auto& host = cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0];
  EXPECT_TRUE(host->canCreateConnection(ResourcePriority::Default));
  // If one connection exists to that host, canCreateConnection will fail.
  host->stats().cx_active_.inc();
  EXPECT_FALSE(host->canCreateConnection(ResourcePriority::Default));
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

  ResolverData resolver(*dns_resolver_, server_context_.dispatcher_);
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);
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

  ResolverData resolver(*dns_resolver_, server_context_.dispatcher_);
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);
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

TEST_F(StrictDnsClusterImplTest, HostUpdateWithDisabledACEndpoint) {
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
                health_check_config:
                  disable_active_health_check: true
  )EOF";

  ResolverData resolver(*dns_resolver_, server_context_.dispatcher_);
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);
  std::shared_ptr<MockHealthChecker> health_checker(new MockHealthChecker());
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  cluster.setHealthChecker(health_checker);
  ReadyWatcher initialized;
  cluster.initialize([&initialized]() { initialized.ready(); });
  EXPECT_CALL(initialized, ready());

  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(*resolver.timer_, enableTimer(_, _)).Times(2);
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                         TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  {
    const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(2UL, hosts.size());
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_FALSE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

    EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
    EXPECT_EQ(2UL, cluster.info()->endpointStats().membership_healthy_.value());
    EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());
  }

  // Re-resolve the DNS name with only one record, we should have 1 host.
  resolver.dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                         TestUtility::makeDnsResponse({"127.0.0.1"}));

  {
    const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
    EXPECT_EQ(1UL, hosts.size());
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::PENDING_DYNAMIC_REMOVAL));
    EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
    EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
    EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_healthy_.value());
    EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());
  }
}

TEST_F(StrictDnsClusterImplTest, LoadAssignmentBasic) {
  // gmock matches in LIFO order which is why these are swapped.
  ResolverData resolver3(*dns_resolver_, server_context_.dispatcher_);
  ResolverData resolver2(*dns_resolver_, server_context_.dispatcher_);
  ResolverData resolver1(*dns_resolver_, server_context_.dispatcher_);

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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);

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

  cluster.info()->trafficStats().upstream_rq_total_.inc();
  EXPECT_EQ(1UL, stats_.counter("cluster.name.upstream_rq_total").value());

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.maintenance_mode.name", 0));
  EXPECT_FALSE(cluster.info()->maintenanceMode());

  ReadyWatcher membership_updated;
  auto priority_update_cb = cluster.prioritySet().addPriorityUpdateCb(
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
            cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->coarseHealth());
  EXPECT_EQ(Host::Health::Degraded,
            cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->coarseHealth());

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
  absl::node_hash_set<HostSharedPtr> removed_hosts;
  auto priority_update_cb2 = cluster.prioritySet().addPriorityUpdateCb(
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

  EXPECT_CALL(resolver1.active_dns_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
  EXPECT_CALL(resolver2.active_dns_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
  EXPECT_CALL(resolver3.active_dns_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
}

TEST_F(StrictDnsClusterImplTest, LoadAssignmentBasicMultiplePriorities) {
  ResolverData resolver3(*dns_resolver_, server_context_.dispatcher_);
  ResolverData resolver2(*dns_resolver_, server_context_.dispatcher_);
  ResolverData resolver1(*dns_resolver_, server_context_.dispatcher_);

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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);
  ReadyWatcher membership_updated;
  auto priority_update_cb = cluster.prioritySet().addPriorityUpdateCb(
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

  EXPECT_CALL(resolver1.active_dns_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
  EXPECT_CALL(resolver2.active_dns_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
  EXPECT_CALL(resolver3.active_dns_query_,
              cancel(Network::ActiveDnsQuery::CancelReason::QueryAbandoned));
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
  Envoy::Stats::ScopeSharedPtr scope =
      stats_.createScope(fmt::format("cluster.{}.", cluster_config.name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);

  EXPECT_THROW_WITH_MESSAGE(std::make_unique<StrictDnsClusterImpl>(
                                server_context_, cluster_config, runtime_, dns_resolver_,
                                factory_context, std::move(scope), false),
                            EnvoyException,
                            "STRICT_DNS clusters must NOT have a custom resolver name set");
}

TEST_F(StrictDnsClusterImplTest, FailureRefreshRateBackoffResetsWhenSuccessHappens) {
  ResolverData resolver(*dns_resolver_, server_context_.dispatcher_);

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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);
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
  ResolverData resolver(*dns_resolver_, server_context_.dispatcher_);

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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver_,
                               factory_context, std::move(scope), false);
  ReadyWatcher membership_updated;
  auto priority_update_cb = cluster.prioritySet().addPriorityUpdateCb(
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  EXPECT_THROW_WITH_REGEX(
      StrictDnsClusterImpl(server_context_, cluster_config, runtime_, dns_resolver_,
                           factory_context, std::move(scope), false),
      EnvoyException,
      R"(the \{hpack_table_size\} HTTP/2 SETTINGS parameter\(s\) can not be configured through)"
      " both");
}

class HostImplTest : public Event::TestUsingSimulatedTime, public testing::Test {};

TEST_F(HostImplTest, HostCluster) {
  MockClusterMockPrioritySet cluster;
  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 1);
  EXPECT_EQ(cluster.info_.get(), &host->cluster());
  EXPECT_EQ("", host->hostname());
  EXPECT_FALSE(host->canary());
  EXPECT_EQ("", host->locality().zone());
}

TEST_F(HostImplTest, Weight) {
  MockClusterMockPrioritySet cluster;

  EXPECT_EQ(1U, makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 0)->weight());
  EXPECT_EQ(128U, makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 128)->weight());
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(),
                         std::numeric_limits<uint32_t>::max())
                ->weight());

  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 50);
  EXPECT_EQ(50U, host->weight());
  host->weight(51);
  EXPECT_EQ(51U, host->weight());
  host->weight(0);
  EXPECT_EQ(1U, host->weight());
  host->weight(std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(), host->weight());
}

TEST_F(HostImplTest, HostnameCanaryAndLocality) {
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
                envoy::config::core::v3::UNKNOWN, simTime());
  EXPECT_EQ(cluster.info_.get(), &host.cluster());
  EXPECT_EQ("lyft.com", host.hostname());
  EXPECT_TRUE(host.canary());
  EXPECT_EQ("oceania", host.locality().region());
  EXPECT_EQ("hello", host.locality().zone());
  EXPECT_EQ("world", host.locality().sub_zone());
  EXPECT_EQ(1, host.priority());
}

TEST_F(HostImplTest, CreateConnection) {
  MockClusterMockPrioritySet cluster;
  envoy::config::core::v3::Metadata metadata;
  Config::Metadata::mutableMetadataValue(metadata, Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);
  envoy::config::core::v3::Locality locality;
  locality.set_region("oceania");
  locality.set_zone("hello");
  locality.set_sub_zone("world");
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.1:1234");
  auto host = std::make_shared<HostImpl>(
      cluster.info_, "lyft.com", address,
      std::make_shared<const envoy::config::core::v3::Metadata>(metadata), 1, locality,
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 1,
      envoy::config::core::v3::UNKNOWN, simTime());

  testing::StrictMock<Event::MockDispatcher> dispatcher;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options;
  Network::ConnectionSocket::OptionsSharedPtr options;
  Network::MockTransportSocketFactory socket_factory;

  auto connection = new testing::StrictMock<Network::MockClientConnection>();
  EXPECT_CALL(*connection, setBufferLimits(0));
  EXPECT_CALL(dispatcher, createClientConnection_(_, _, _, _)).WillOnce(Return(connection));
  EXPECT_CALL(*connection, connectionInfoSetter());
  Envoy::Upstream::Host::CreateConnectionData connection_data =
      host->createConnection(dispatcher, options, transport_socket_options);
  EXPECT_EQ(connection, connection_data.connection_.get());
}

TEST_F(HostImplTest, CreateConnectionHappyEyeballs) {
  MockClusterMockPrioritySet cluster;
  envoy::config::core::v3::Metadata metadata;
  Config::Metadata::mutableMetadataValue(metadata, Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);
  envoy::config::core::v3::Locality locality;
  locality.set_region("oceania");
  locality.set_zone("hello");
  locality.set_sub_zone("world");
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.1:1234");
  auto host = std::make_shared<HostImpl>(
      cluster.info_, "lyft.com", address,
      std::make_shared<const envoy::config::core::v3::Metadata>(metadata), 1, locality,
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 1,
      envoy::config::core::v3::UNKNOWN, simTime());

  testing::StrictMock<Event::MockDispatcher> dispatcher;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options;
  Network::ConnectionSocket::OptionsSharedPtr options;
  Network::MockTransportSocketFactory socket_factory;

  std::vector<Network::Address::InstanceConstSharedPtr> address_list = {
      Network::Utility::resolveUrl("tcp://10.0.0.1:1235"),
      address,
  };
  host->setAddressList(address_list);
  auto connection = new testing::StrictMock<Network::MockClientConnection>();
  EXPECT_CALL(*connection, setBufferLimits(0));
  EXPECT_CALL(*connection, addConnectionCallbacks(_));
  EXPECT_CALL(*connection, connectionInfoSetter());
  // The underlying connection should be created with the first address in the list.
  EXPECT_CALL(dispatcher, createClientConnection_(address_list[0], _, _, _))
      .WillOnce(Return(connection));
  EXPECT_CALL(dispatcher, createTimer_(_));

  Envoy::Upstream::Host::CreateConnectionData connection_data =
      host->createConnection(dispatcher, options, transport_socket_options);
  // The created connection will be wrapped in a HappyEyeballsConnectionImpl.
  EXPECT_NE(connection, connection_data.connection_.get());
}

TEST_F(HostImplTest, ProxyOverridesHappyEyeballs) {
  MockClusterMockPrioritySet cluster;
  envoy::config::core::v3::Metadata metadata;
  Config::Metadata::mutableMetadataValue(metadata, Config::MetadataFilters::get().ENVOY_LB,
                                         Config::MetadataEnvoyLbKeys::get().CANARY)
      .set_bool_value(true);
  envoy::config::core::v3::Locality locality;
  locality.set_region("oceania");
  locality.set_zone("hello");
  locality.set_sub_zone("world");
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://10.0.0.1:1234");
  Network::Address::InstanceConstSharedPtr proxy_address =
      Network::Utility::resolveUrl("tcp://10.0.0.1:9999");
  auto host = std::make_shared<HostImpl>(
      cluster.info_, "lyft.com", address,
      std::make_shared<const envoy::config::core::v3::Metadata>(metadata), 1, locality,
      envoy::config::endpoint::v3::Endpoint::HealthCheckConfig::default_instance(), 1,
      envoy::config::core::v3::UNKNOWN, simTime());

  testing::StrictMock<Event::MockDispatcher> dispatcher;
  auto proxy_info = std::make_unique<Network::TransportSocketOptions::Http11ProxyInfo>(
      "www.google.com", proxy_address);
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options =
      std::make_shared<Network::TransportSocketOptionsImpl>(
          "name", std::vector<std::string>(), std::vector<std::string>(),
          std::vector<std::string>(), absl::nullopt, nullptr, std::move(proxy_info));
  Network::ConnectionSocket::OptionsSharedPtr options;
  Network::MockTransportSocketFactory socket_factory;

  std::vector<Network::Address::InstanceConstSharedPtr> address_list = {
      Network::Utility::resolveUrl("tcp://10.0.0.1:1235"),
      address,
  };
  host->setAddressList(address_list);
  auto connection = new testing::StrictMock<Network::MockClientConnection>();
  EXPECT_CALL(*connection, setBufferLimits(0));
  EXPECT_CALL(*connection, connectionInfoSetter());
  // The underlying connection should be created to the proxy address.
  EXPECT_CALL(dispatcher, createClientConnection_(proxy_address, _, _, _))
      .WillOnce(Return(connection));

  Envoy::Upstream::Host::CreateConnectionData connection_data =
      host->createConnection(dispatcher, options, transport_socket_options);
  // The created connection will be a raw connection to the proxy address rather
  // than a happy eyeballs connection.
  EXPECT_EQ(connection, connection_data.connection_.get());
}

TEST_F(HostImplTest, HealthFlags) {
  MockClusterMockPrioritySet cluster;
  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 1);

  // To begin with, no flags are set so we're healthy.
  EXPECT_EQ(Host::Health::Healthy, host->coarseHealth());

  // Setting an unhealthy flag make the host unhealthy.
  host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Unhealthy, host->coarseHealth());

  // Setting a degraded flag on an unhealthy host has no effect.
  host->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Unhealthy, host->coarseHealth());

  // If the degraded flag is the only thing set, host is degraded.
  host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Degraded, host->coarseHealth());

  // If the EDS and active degraded flag is set, host is degraded.
  host->healthFlagSet(Host::HealthFlag::DEGRADED_EDS_HEALTH);
  EXPECT_EQ(Host::Health::Degraded, host->coarseHealth());

  // If only the EDS degraded is set, host is degraded.
  host->healthFlagClear(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Degraded, host->coarseHealth());

  // If EDS and failed active hc is set, host is unhealthy.
  host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(Host::Health::Unhealthy, host->coarseHealth());
}

TEST_F(HostImplTest, HealthStatus) {
  MockClusterMockPrioritySet cluster;
  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 1, 0,
                                    Host::HealthStatus::DRAINING);

  // To begin with, no flags are set so EDS status is used.
  EXPECT_EQ(Host::HealthStatus::DRAINING, host->healthStatus());

  // Setting an active unhealthy flag make the host unhealthy.
  host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ(Host::HealthStatus::UNHEALTHY, host->healthStatus());
  host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  EXPECT_EQ(Host::HealthStatus::UNHEALTHY, host->healthStatus());

  // Setting a degraded flag on an unhealthy host has no effect.
  host->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  EXPECT_EQ(Host::HealthStatus::UNHEALTHY, host->healthStatus());

  // If the degraded flag is the only thing set, host is degraded.
  host->healthFlagClear(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  EXPECT_EQ(Host::HealthStatus::DEGRADED, host->healthStatus());
}

TEST_F(HostImplTest, SkipActiveHealthCheckFlag) {
  MockClusterMockPrioritySet cluster;
  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 1);

  // To begin with, the default setting is false.
  EXPECT_EQ(false, host->disableActiveHealthCheck());
  host->setDisableActiveHealthCheck(true);
  EXPECT_EQ(true, host->disableActiveHealthCheck());
}

// Test that it's not possible to do a HostDescriptionImpl with a unix
// domain socket host and a health check config with non-zero port.
// This is a regression test for oss-fuzz issue
// https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=11095
TEST_F(HostImplTest, HealthPipeAddress) {
  EXPECT_THROW_WITH_MESSAGE(
      {
        std::shared_ptr<MockClusterInfo> info{new NiceMock<MockClusterInfo>()};
        envoy::config::endpoint::v3::Endpoint::HealthCheckConfig config;
        config.set_port_value(8000);
        HostDescriptionImpl descr(info, "", Network::Utility::resolveUrl("unix://foo"), nullptr,
                                  envoy::config::core::v3::Locality().default_instance(), config, 1,
                                  simTime());
      },
      EnvoyException, "Invalid host configuration: non-zero port for non-IP address");
}

TEST_F(HostImplTest, HostAddressList) {
  MockClusterMockPrioritySet cluster;
  HostSharedPtr host = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 1);
  const std::vector<Network::Address::InstanceConstSharedPtr> address_list = {};
  EXPECT_EQ(address_list, host->addressList());
}

// Test that hostname flag from the health check config propagates.
TEST_F(HostImplTest, HealthcheckHostname) {
  std::shared_ptr<MockClusterInfo> info{new NiceMock<MockClusterInfo>()};
  envoy::config::endpoint::v3::Endpoint::HealthCheckConfig config;
  config.set_hostname("foo");
  HostDescriptionImpl descr(info, "", Network::Utility::resolveUrl("tcp://1.2.3.4:80"), nullptr,
                            envoy::config::core::v3::Locality().default_instance(), config, 1,
                            simTime());
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(Host::Health::Degraded,
            cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->coarseHealth());
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
  cluster.initialize([] {});
  // Increment a stat and verify it is emitted with alt_stat_name
  cluster.info()->trafficStats().upstream_rq_total_.inc();
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), true);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(LoadBalancerType::RingHash, cluster.info()->lbType());
  EXPECT_TRUE(cluster.info()->addedViaApi());
}

TEST_F(StaticClusterImplTest, RoundRobinWithSlowStart) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: static
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11001
    round_robin_lb_config:
      slow_start_config:
        slow_start_window: 60s
        aggression:
          default_value: 2.0
          runtime_key: a_key
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);

  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), true);
  cluster.initialize([] {});

  EXPECT_EQ(LoadBalancerType::RoundRobin, cluster.info()->lbType());
  auto slow_start_config = cluster.info()->lbRoundRobinConfig()->slow_start_config();
  EXPECT_EQ(std::chrono::milliseconds(60000),
            std::chrono::milliseconds(
                DurationUtil::durationToMilliseconds(slow_start_config.slow_start_window())));
  EXPECT_EQ(2.0, slow_start_config.aggression().default_value());
  EXPECT_TRUE(cluster.info()->addedViaApi());
}

TEST_F(StaticClusterImplTest, LeastRequestWithSlowStart) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: static
    lb_policy: least_request
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11001
    least_request_lb_config:
      slow_start_config:
        slow_start_window: 60s
        aggression:
          default_value: 2.0
          runtime_key: a_key
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);

  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), true);
  cluster.initialize([] {});

  EXPECT_EQ(LoadBalancerType::LeastRequest, cluster.info()->lbType());
  auto slow_start_config = cluster.info()->lbLeastRequestConfig()->slow_start_config();
  EXPECT_EQ(std::chrono::milliseconds(60000),
            std::chrono::milliseconds(
                DurationUtil::durationToMilliseconds(slow_start_config.slow_start_window())));
  EXPECT_EQ(2.0, slow_start_config.aggression().default_value());
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);

  Outlier::MockDetector* detector = new Outlier::MockDetector();
  EXPECT_CALL(*detector, addChangedStateCb(_));
  cluster.setOutlierDetector(Outlier::DetectorSharedPtr{detector});
  cluster.initialize([] {});

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->endpointStats().membership_healthy_.value());

  // Set a single host as having failed and fire outlier detector callbacks. This should result
  // in only a single healthy host.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->outlierDetector().putHttpResponseCode(
      503);
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_NE(cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts()[0],
            cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);

  // Bring the host back online.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->endpointStats().membership_healthy_.value());
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);

  Outlier::MockDetector* outlier_detector = new NiceMock<Outlier::MockDetector>();
  cluster.setOutlierDetector(Outlier::DetectorSharedPtr{outlier_detector});

  std::shared_ptr<MockHealthChecker> health_checker(new NiceMock<MockHealthChecker>());
  cluster.setHealthChecker(health_checker);

  ReadyWatcher initialized;
  cluster.initialize([&initialized] { initialized.ready(); });

  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());

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
  EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                               HealthTransition::Changed);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                               HealthTransition::Changed);
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(2UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagSet(
      Host::HealthFlag::FAILED_OUTLIER_CHECK);
  outlier_detector->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());

  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagSet(
      Host::HealthFlag::DEGRADED_ACTIVE_HC);
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_degraded_.value());

  // Mark the endpoint as unhealthy. This should decrement the degraded stat.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagSet(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());

  // Go back to degraded.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_degraded_.value());

  // Then go healthy.
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagClear(
      Host::HealthFlag::DEGRADED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1],
                               HealthTransition::Changed);
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->degradedHosts().size());
  EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());
}

TEST_F(StaticClusterImplTest, InitialHostsDisableHC) {
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
                    port_value: 11001
                health_check_config:
                  disable_active_health_check: true
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11002
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);

  Outlier::MockDetector* outlier_detector = new NiceMock<Outlier::MockDetector>();
  cluster.setOutlierDetector(Outlier::DetectorSharedPtr{outlier_detector});

  std::shared_ptr<MockHealthChecker> health_checker(new NiceMock<MockHealthChecker>());
  cluster.setHealthChecker(health_checker);

  ReadyWatcher initialized;
  cluster.initialize([&initialized] { initialized.ready(); });

  // The endpoint with disabled active health check should not be set FAILED_ACTIVE_HC
  // at beginning.
  const auto& hosts = cluster.prioritySet().hostSetsPerPriority()[0]->hosts();
  EXPECT_EQ(2UL, hosts.size());
  EXPECT_FALSE(hosts[0]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));
  EXPECT_TRUE(hosts[1]->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC));

  // The endpoint with disabled active health check is considered healthy.
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(1UL, cluster.info()->endpointStats().membership_healthy_.value());
  EXPECT_EQ(0UL, cluster.info()->endpointStats().membership_degraded_.value());

  // Perform a health check for the second host, and then the initialization is finished.
  EXPECT_CALL(initialized, ready());
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[1]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                               HealthTransition::Changed);
  EXPECT_EQ(2UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
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
  cluster.prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthChecker().setUnhealthy(
      HealthCheckHostMonitor::UnhealthyType::ImmediateHealthCheckFail);
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

  EXPECT_THROW(
      {
        envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
        Envoy::Stats::ScopeSharedPtr scope =
            stats_.createScope(fmt::format("cluster.{}.", cluster_config.alt_stat_name().empty()
                                                              ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
        Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
            server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
            validation_visitor_);
        StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                                  std::move(scope), false);
      },
      EnvoyException);
}

// load_balancing_policy should be used when lb_policy is set to LOAD_BALANCING_POLICY_CONFIG.
TEST_F(StaticClusterImplTest, LoadBalancingPolicyWithLbPolicy) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});

  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: static
    lb_policy: LOAD_BALANCING_POLICY_CONFIG
    load_balancing_policy:
      policies:
        - typed_extension_config:
            name: custom_lb
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11001
  )EOF";

  NiceMock<MockTypedLoadBalancerFactory> factory;
  EXPECT_CALL(factory, name()).WillRepeatedly(Return("custom_lb"));
  Registry::InjectFactory<TypedLoadBalancerFactory> registered_factory(factory);

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), true);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(LoadBalancerType::LoadBalancingPolicyConfig, cluster.info()->lbType());
  EXPECT_TRUE(cluster.info()->addedViaApi());
}

// load_balancing_policy should also be used when lb_policy is set to something else besides
// LOAD_BALANCING_POLICY_CONFIG.
TEST_F(StaticClusterImplTest, LoadBalancingPolicyWithOtherLbPolicy) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});

  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: static
    lb_policy: ROUND_ROBIN
    load_balancing_policy:
      policies:
        - typed_extension_config:
            name: custom_lb
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11001
  )EOF";

  NiceMock<MockTypedLoadBalancerFactory> factory;
  EXPECT_CALL(factory, name()).WillRepeatedly(Return("custom_lb"));
  Registry::InjectFactory<TypedLoadBalancerFactory> registered_factory(factory);

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), true);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(LoadBalancerType::LoadBalancingPolicyConfig, cluster.info()->lbType());
  EXPECT_TRUE(cluster.info()->addedViaApi());
}

// load_balancing_policy should also be used when lb_policy is omitted.
TEST_F(StaticClusterImplTest, LoadBalancingPolicyWithoutLbPolicy) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});

  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: static
    load_balancing_policy:
      policies:
        - typed_extension_config:
            name: custom_lb
    load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: 10.0.0.1
                    port_value: 11001
  )EOF";

  NiceMock<MockTypedLoadBalancerFactory> factory;
  EXPECT_CALL(factory, name()).WillRepeatedly(Return("custom_lb"));
  Registry::InjectFactory<TypedLoadBalancerFactory> registered_factory(factory);

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), true);
  cluster.initialize([] {});

  EXPECT_EQ(1UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(LoadBalancerType::LoadBalancingPolicyConfig, cluster.info()->lbType());
  EXPECT_TRUE(cluster.info()->addedViaApi());
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  EXPECT_THROW_WITH_MESSAGE(StaticClusterImpl(server_context_, cluster_config, runtime_,
                                              factory_context, std::move(scope), false),
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
  Envoy::Stats::ScopeSharedPtr scope =
      stats_.createScope(fmt::format("cluster.{}.", cluster_config.name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                            std::move(scope), false);
  cluster.initialize([] {});

  EXPECT_EQ(0UL, cluster.prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

TEST_F(StaticClusterImplTest, SourceAddressPriorityWitExtraSourceAddress) {
  envoy::config::cluster::v3::Cluster config;
  config.set_name("staticcluster");
  config.mutable_connect_timeout();

  {
    // If the cluster manager gets a source address from the bootstrap proto, use it.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("3.4.5.6", 80, nullptr);
    EXPECT_EQ("1.2.3.5:0", cluster.info()
                               ->getUpstreamLocalAddressSelector()
                               ->getUpstreamLocalAddress(remote_address, nullptr)
                               .address_->asString());
  }

  {
    // Test extra_source_addresses from bootstrap.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.add_extra_source_addresses()
        ->mutable_address()
        ->set_address("2001::1");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("3.4.5.6", 80, nullptr);
    EXPECT_EQ("1.2.3.5:0", cluster.info()
                               ->getUpstreamLocalAddressSelector()
                               ->getUpstreamLocalAddress(remote_address, nullptr)
                               .address_->asString());
    Network::Address::InstanceConstSharedPtr v6_remote_address =
        std::make_shared<Network::Address::Ipv6Instance>("2001::3", 80, nullptr);
    EXPECT_EQ("[2001::1]:0", cluster.info()
                                 ->getUpstreamLocalAddressSelector()
                                 ->getUpstreamLocalAddress(v6_remote_address, nullptr)
                                 .address_->asString());
  }

  {
    // Test no same IP version in multiple source addresses.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.clear_extra_source_addresses();
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr v6_remote_address =
        std::make_shared<Network::Address::Ipv6Instance>("2001::3", 80, nullptr);
    EXPECT_EQ("1.2.3.5:0", cluster.info()
                               ->getUpstreamLocalAddressSelector()
                               ->getUpstreamLocalAddress(v6_remote_address, nullptr)
                               .address_->asString());
  }

  {
    // Test two same IP version addresses.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.clear_extra_source_addresses();
    server_context_.cluster_manager_.bind_config_.add_extra_source_addresses()
        ->mutable_address()
        ->set_address("1.2.3.6");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    EXPECT_THROW_WITH_MESSAGE(
        StaticClusterImpl cluster(server_context_, config, runtime_, factory_context,
                                  std::move(scope), false),
        EnvoyException,
        "Bootstrap's upstream binding config has two same IP version source addresses. Only two "
        "different IP version source addresses can be supported in BindConfig's source_address and "
        "extra_source_addresses fields");
  }

  {
    // Test more than two multiple source addresses
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.clear_extra_source_addresses();
    server_context_.cluster_manager_.bind_config_.add_extra_source_addresses()
        ->mutable_address()
        ->set_address("2001::1");
    server_context_.cluster_manager_.bind_config_.add_extra_source_addresses()
        ->mutable_address()
        ->set_address("2001::2");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    EXPECT_THROW_WITH_MESSAGE(
        StaticClusterImpl cluster(server_context_, config, runtime_, factory_context,
                                  std::move(scope), false),
        EnvoyException,
        "Bootstrap's upstream binding config has more than one extra source addresses. Only "
        "one extra source can be supported in BindConfig's extra_source_addresses field");
  }

  {
    // Test non IP address case.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.clear_extra_source_addresses();
    server_context_.cluster_manager_.bind_config_.add_extra_source_addresses()
        ->mutable_address()
        ->set_address("2001::1");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr v6_remote_address =
        std::make_shared<Network::Address::PipeInstance>("/test");
    EXPECT_EQ("1.2.3.5:0", cluster.info()
                               ->getUpstreamLocalAddressSelector()
                               ->getUpstreamLocalAddress(v6_remote_address, nullptr)
                               .address_->asString());
  }

  const std::string cluster_address = "5.6.7.8";
  config.mutable_upstream_bind_config()->mutable_source_address()->set_address(cluster_address);
  {
    // Verify source address from cluster config is used when present.
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("3.4.5.6", 80, nullptr);
    EXPECT_EQ(cluster_address, cluster.info()
                                   ->getUpstreamLocalAddressSelector()
                                   ->getUpstreamLocalAddress(remote_address, nullptr)
                                   .address_->ip()
                                   ->addressAsString());
  }

  {
    // Test cluster config has more than two multiple source addresses
    config.mutable_upstream_bind_config()->mutable_source_address()->set_address(cluster_address);
    config.mutable_upstream_bind_config()
        ->add_extra_source_addresses()
        ->mutable_address()
        ->set_address("2001::1");
    config.mutable_upstream_bind_config()
        ->add_extra_source_addresses()
        ->mutable_address()
        ->set_address("2001::2");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    EXPECT_THROW_WITH_MESSAGE(StaticClusterImpl cluster(server_context_, config, runtime_,
                                                        factory_context, std::move(scope), false),
                              EnvoyException,
                              "Cluster staticcluster's upstream binding config has more than one "
                              "extra source addresses. Only one extra source can be "
                              "supported in BindConfig's extra_source_addresses field");
  }

  {
    // The source address from cluster config takes precedence over one from the bootstrap proto.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    config.mutable_upstream_bind_config()->clear_extra_source_addresses();
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("3.4.5.6", 80, nullptr);
    EXPECT_EQ(cluster_address, cluster.info()
                                   ->getUpstreamLocalAddressSelector()
                                   ->getUpstreamLocalAddress(remote_address, nullptr)
                                   .address_->ip()
                                   ->addressAsString());
  }
}

TEST_F(StaticClusterImplTest, SourceAddressPriorityWithDeprecatedAdditionalSourceAddress) {
  envoy::config::cluster::v3::Cluster config;
  config.set_name("staticcluster");
  config.mutable_connect_timeout();

  {
    // Test more than two multiple source addresses
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.add_additional_source_addresses()->set_address(
        "2001::1");
    server_context_.cluster_manager_.bind_config_.add_extra_source_addresses()
        ->mutable_address()
        ->set_address("2001::1");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    EXPECT_THROW_WITH_MESSAGE(
        StaticClusterImpl cluster(server_context_, config, runtime_, factory_context,
                                  std::move(scope), false),
        EnvoyException,
        "Can't specify both `extra_source_addresses` and `additional_source_addresses` in the "
        "Bootstrap's upstream binding config");
    server_context_.cluster_manager_.bind_config_.clear_extra_source_addresses();
    server_context_.cluster_manager_.bind_config_.clear_additional_source_addresses();
  }

  {
    // Test additional_source_addresses from bootstrap.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.add_additional_source_addresses()->set_address(
        "2001::1");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("3.4.5.6", 80, nullptr);
    EXPECT_EQ("1.2.3.5:0", cluster.info()
                               ->getUpstreamLocalAddressSelector()
                               ->getUpstreamLocalAddress(remote_address, nullptr)
                               .address_->asString());
    Network::Address::InstanceConstSharedPtr v6_remote_address =
        std::make_shared<Network::Address::Ipv6Instance>("2001::3", 80, nullptr);
    EXPECT_EQ("[2001::1]:0", cluster.info()
                                 ->getUpstreamLocalAddressSelector()
                                 ->getUpstreamLocalAddress(v6_remote_address, nullptr)
                                 .address_->asString());
  }

  {
    // Test no same IP version in multiple source addresses.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.clear_additional_source_addresses();
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr v6_remote_address =
        std::make_shared<Network::Address::Ipv6Instance>("2001::3", 80, nullptr);
    EXPECT_EQ("1.2.3.5:0", cluster.info()
                               ->getUpstreamLocalAddressSelector()
                               ->getUpstreamLocalAddress(v6_remote_address, nullptr)
                               .address_->asString());
  }

  {
    // Test two same IP version addresses.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.clear_additional_source_addresses();
    server_context_.cluster_manager_.bind_config_.add_additional_source_addresses()->set_address(
        "1.2.3.6");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    EXPECT_THROW_WITH_MESSAGE(
        StaticClusterImpl cluster(server_context_, config, runtime_, factory_context,
                                  std::move(scope), false),
        EnvoyException,
        "Bootstrap's upstream binding config has two same IP version source addresses. Only two "
        "different IP version source addresses can be supported in BindConfig's source_address and "
        "additional_source_addresses fields");
  }

  {
    // Test more than two multiple source addresses
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.clear_additional_source_addresses();
    server_context_.cluster_manager_.bind_config_.add_additional_source_addresses()->set_address(
        "2001::1");
    server_context_.cluster_manager_.bind_config_.add_additional_source_addresses()->set_address(
        "2001::2");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    EXPECT_THROW_WITH_MESSAGE(
        StaticClusterImpl cluster(server_context_, config, runtime_, factory_context,
                                  std::move(scope), false),
        EnvoyException,
        "Bootstrap's upstream binding config has more than one additional source addresses. Only "
        "one additional source can be supported in BindConfig's additional_source_addresses field");
  }

  {
    // Test non IP address case.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    server_context_.cluster_manager_.bind_config_.clear_additional_source_addresses();
    server_context_.cluster_manager_.bind_config_.add_additional_source_addresses()->set_address(
        "2001::1");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr v6_remote_address =
        std::make_shared<Network::Address::PipeInstance>("/test");
    EXPECT_EQ("1.2.3.5:0", cluster.info()
                               ->getUpstreamLocalAddressSelector()
                               ->getUpstreamLocalAddress(v6_remote_address, nullptr)
                               .address_->asString());
  }

  const std::string cluster_address = "5.6.7.8";
  config.mutable_upstream_bind_config()->mutable_source_address()->set_address(cluster_address);
  {
    // Test cluster config has more than two multiple source addresses
    config.mutable_upstream_bind_config()->mutable_source_address()->set_address(cluster_address);
    config.mutable_upstream_bind_config()->add_additional_source_addresses()->set_address(
        "2001::1");
    config.mutable_upstream_bind_config()->add_additional_source_addresses()->set_address(
        "2001::2");
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    EXPECT_THROW_WITH_MESSAGE(StaticClusterImpl cluster(server_context_, config, runtime_,
                                                        factory_context, std::move(scope), false),
                              EnvoyException,
                              "Cluster staticcluster's upstream binding config has more than one "
                              "additional source addresses. Only one additional source can be "
                              "supported in BindConfig's additional_source_addresses field");
  }

  {
    // The source address from cluster config takes precedence over one from the bootstrap proto.
    server_context_.cluster_manager_.bind_config_.mutable_source_address()->set_address("1.2.3.5");
    config.mutable_upstream_bind_config()->clear_additional_source_addresses();
    Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
        "cluster.{}.", config.alt_stat_name().empty() ? config.name() : config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
        validation_visitor_);
    StaticClusterImpl cluster(server_context_, config, runtime_, factory_context, std::move(scope),
                              false);
    Network::Address::InstanceConstSharedPtr remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("3.4.5.6", 80, nullptr);
    EXPECT_EQ(cluster_address, cluster.info()
                                   ->getUpstreamLocalAddressSelector()
                                   ->getUpstreamLocalAddress(remote_address, nullptr)
                                   .address_->ip()
                                   ->addressAsString());
  }
}

// LEDS is not supported with a static cluster at the moment.
TEST_F(StaticClusterImplTest, LedsUnsupported) {
  const std::string yaml = R"EOF(
    name: staticcluster
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
        endpoints:
          leds_cluster_locality_config:
            leds_collection_name: xdstp://foo/leds_collection_name
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);
  EXPECT_THROW_WITH_MESSAGE(
      StaticClusterImpl cluster(server_context_, cluster_config, runtime_, factory_context,
                                std::move(scope), false),
      EnvoyException,
      "LEDS is only supported when EDS is used. Static cluster staticcluster cannot use LEDS.");
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
  Envoy::Stats::ScopeSharedPtr scope = stats_.createScope(fmt::format(
      "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                            : cluster_config.alt_stat_name()));
  Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
      server_context_, ssl_context_manager_, *scope, server_context_.cluster_manager_, stats_,
      validation_visitor_);

  StrictDnsClusterImpl cluster(server_context_, cluster_config, runtime_, dns_resolver,
                               factory_context, std::move(scope), false);
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
  auto priority_update_cb = priority_set.addPriorityUpdateCb(
      [&](uint32_t priority, const HostVector&, const HostVector&) -> void {
        last_priority = priority;
        ++priority_changes;
      });
  auto member_update_cb = priority_set.addMemberUpdateCb(
      [&](const HostVector&, const HostVector&) -> void { ++membership_changes; });

  // The initial priority set starts with priority level 0.
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
  auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info, "tcp://127.0.0.1:80", *time_source)}));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();
  HostMapConstSharedPtr fake_cross_priority_host_map = std::make_shared<HostMap>();
  {
    HostVector hosts_added{hosts->front()};
    HostVector hosts_removed{};

    priority_set.updateHosts(
        1,
        updateHostsParams(hosts, hosts_per_locality,
                          std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
        {}, hosts_added, hosts_removed, absl::nullopt, fake_cross_priority_host_map);
  }
  EXPECT_EQ(1, priority_changes);
  EXPECT_EQ(1, membership_changes);
  EXPECT_EQ(last_priority, 1);
  EXPECT_EQ(1, priority_set.hostSetsPerPriority()[1]->hosts().size());

  // Simply verify the set and get the cross-priority host map is working properly in the priority
  // set.
  EXPECT_EQ(fake_cross_priority_host_map.get(), priority_set.crossPriorityHostMap().get());

  // Test iteration.
  int i = 0;
  for (auto& host_set : priority_set.hostSetsPerPriority()) {
    EXPECT_EQ(host_set.get(), priority_set.hostSetsPerPriority()[i++].get());
  }

  // Test batch host updates. Verify that we can move a host without triggering intermediate host
  // updates.

  // We're going to do a noop host change, so add a callback to assert that we're not announcing
  // any host changes.
  auto member_update_cb2 = priority_set.addMemberUpdateCb(
      [&](const HostVector& added, const HostVector& removed) -> void {
        EXPECT_TRUE(added.empty() && removed.empty());
      });

  TestBatchUpdateCb batch_update(hosts, hosts_per_locality);
  priority_set.batchHostUpdate(batch_update);

  // We expect to see two priority changes, but only one membership change.
  EXPECT_EQ(3, priority_changes);
  EXPECT_EQ(2, membership_changes);
}

// Helper class used to test MainPrioritySetImpl.
class TestMainPrioritySetImpl : public MainPrioritySetImpl {
public:
  HostMapConstSharedPtr constHostMapForTest() { return const_cross_priority_host_map_; }
  HostMapSharedPtr mutableHostMapForTest() { return mutable_cross_priority_host_map_; }
};

// Test that the priority set in the main thread can work correctly.
TEST(PrioritySet, MainPrioritySetTest) {
  TestMainPrioritySetImpl priority_set;
  priority_set.getOrCreateHostSet(0);

  std::shared_ptr<MockClusterInfo> info{new NiceMock<MockClusterInfo>()};
  auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
  HostVectorSharedPtr hosts(
      new HostVector({makeTestHost(info, "tcp://127.0.0.1:80", *time_source)}));
  HostsPerLocalitySharedPtr hosts_per_locality = std::make_shared<HostsPerLocalityImpl>();

  // The host map is initially empty or null.
  EXPECT_TRUE(priority_set.constHostMapForTest()->empty());
  EXPECT_EQ(nullptr, priority_set.mutableHostMapForTest().get());

  {
    HostVector hosts_added{hosts->front()};
    HostVector hosts_removed{};

    priority_set.updateHosts(1,
                             updateHostsParams(hosts, hosts_per_locality,
                                               std::make_shared<const HealthyHostVector>(*hosts),
                                               hosts_per_locality),
                             {}, hosts_added, hosts_removed, absl::nullopt);
  }

  // Only mutable host map can be updated directly. Read only host map will not be updated before
  // `crossPriorityHostMap` is called.
  EXPECT_TRUE(priority_set.constHostMapForTest()->empty());
  EXPECT_FALSE(priority_set.mutableHostMapForTest()->empty());

  // Mutable host map will be moved to read only host map after `crossPriorityHostMap` is called.
  HostMapSharedPtr host_map = priority_set.mutableHostMapForTest();
  EXPECT_EQ(host_map.get(), priority_set.crossPriorityHostMap().get());
  EXPECT_EQ(nullptr, priority_set.mutableHostMapForTest().get());

  {
    HostVector hosts_added{};
    HostVector hosts_removed{hosts->front()};

    priority_set.updateHosts(1,
                             updateHostsParams(hosts, hosts_per_locality,
                                               std::make_shared<const HealthyHostVector>(*hosts),
                                               hosts_per_locality),
                             {}, hosts_added, hosts_removed, absl::nullopt);
  }

  // New mutable host map will be created and all update will be applied to new mutable host map.
  // Read only host map will not be updated before `crossPriorityHostMap` is called.
  EXPECT_EQ(host_map.get(), priority_set.constHostMapForTest().get());
  EXPECT_TRUE((priority_set.mutableHostMapForTest().get() != nullptr &&
               priority_set.mutableHostMapForTest().get() != host_map.get()));

  // Again, mutable host map will be moved to read only host map after `crossPriorityHostMap` is
  // called.
  host_map = priority_set.mutableHostMapForTest();
  EXPECT_EQ(host_map.get(), priority_set.crossPriorityHostMap().get());
  EXPECT_EQ(nullptr, priority_set.mutableHostMapForTest().get());
}

class ClusterInfoImplTest : public testing::Test {
public:
  ClusterInfoImplTest() : api_(Api::createApiForTest(stats_, random_)) {
    ON_CALL(server_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  std::unique_ptr<StrictDnsClusterImpl> makeCluster(const std::string& yaml) {
    cluster_config_ = parseClusterFromV3Yaml(yaml);
    scope_ = stats_.createScope(fmt::format("cluster.{}.", cluster_config_.alt_stat_name().empty()
                                                               ? cluster_config_.name()
                                                               : cluster_config_.alt_stat_name()));
    factory_context_ = std::make_unique<Server::Configuration::TransportSocketFactoryContextImpl>(
        server_context_, ssl_context_manager_, *scope_, server_context_.cluster_manager_, stats_,
        validation_visitor_);

    return std::make_unique<StrictDnsClusterImpl>(server_context_, cluster_config_, runtime_,
                                                  dns_resolver_, *factory_context_,
                                                  std::move(scope_), false);
  }

  class RetryBudgetTestClusterInfo : public ClusterInfoImpl {
  public:
    static std::pair<absl::optional<double>, absl::optional<uint32_t>> getRetryBudgetParams(
        const envoy::config::cluster::v3::CircuitBreakers::Thresholds& thresholds) {
      return ClusterInfoImpl::getRetryBudgetParams(thresholds);
    }
  };

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Ssl::MockContextManager> ssl_context_manager_;
  std::shared_ptr<Network::MockDnsResolver> dns_resolver_{new NiceMock<Network::MockDnsResolver>()};
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  ReadyWatcher initialized_;
  envoy::config::cluster::v3::Cluster cluster_config_;
  Envoy::Stats::ScopeSharedPtr scope_;
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

  std::unique_ptr<const Envoy::Config::TypedMetadata::Object>
  parse(const ProtobufWkt::Any&) const override {
    return nullptr;
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

// Verify retry budget default values are honored.
TEST_F(ClusterInfoImplTest, RetryBudgetDefaultPopulation) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: MAGLEV
    load_assignment:
    metadata: { filter_metadata: { com.bar.foo: { baz: test_value },
                                   baz: {name: meh } } }
    circuit_breakers:
      thresholds:
      # 0 - No budget config.
      - priority: DEFAULT
        max_retries: 123
      # 1 - Empty budget config, should have defaults.
      - priority: DEFAULT
        retry_budget: {}
      # 2 - Empty budget config with max retries, should have budget defaults.
      - priority: DEFAULT
        max_retries: 123
        retry_budget: {}
      # 3 - Budget percent set, expect retry concurrency default.
      - priority: DEFAULT
        retry_budget:
          budget_percent:
            value: 42.0
      # 4 - Retry concurrency set, expect budget default.
      - priority: DEFAULT
        retry_budget:
          min_retry_concurrency: 123
  )EOF";

  makeCluster(yaml);
  absl::optional<double> budget_percent;
  absl::optional<uint32_t> min_retry_concurrency;
  auto threshold = cluster_config_.circuit_breakers().thresholds();

  std::tie(budget_percent, min_retry_concurrency) =
      RetryBudgetTestClusterInfo::getRetryBudgetParams(threshold[0]);
  EXPECT_EQ(budget_percent, absl::nullopt);
  EXPECT_EQ(min_retry_concurrency, absl::nullopt);

  std::tie(budget_percent, min_retry_concurrency) =
      RetryBudgetTestClusterInfo::getRetryBudgetParams(threshold[1]);
  EXPECT_EQ(budget_percent, 20.0);
  EXPECT_EQ(min_retry_concurrency, 3UL);

  std::tie(budget_percent, min_retry_concurrency) =
      RetryBudgetTestClusterInfo::getRetryBudgetParams(threshold[2]);
  EXPECT_EQ(budget_percent, 20.0);
  EXPECT_EQ(min_retry_concurrency, 3UL);

  std::tie(budget_percent, min_retry_concurrency) =
      RetryBudgetTestClusterInfo::getRetryBudgetParams(threshold[3]);
  EXPECT_EQ(budget_percent, 42.0);
  EXPECT_EQ(min_retry_concurrency, 3UL);

  std::tie(budget_percent, min_retry_concurrency) =
      RetryBudgetTestClusterInfo::getRetryBudgetParams(threshold[4]);
  EXPECT_EQ(budget_percent, 20.0);
  EXPECT_EQ(min_retry_concurrency, 123UL);
}

TEST_F(ClusterInfoImplTest, UnsupportedPerHostFields) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    lb_policy: MAGLEV
    load_assignment:
    metadata: { filter_metadata: { com.bar.foo: { baz: test_value },
                                   baz: {name: meh } } }
    circuit_breakers:
      per_host_thresholds:
      - priority: DEFAULT
        max_retries: 123
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml), EnvoyException,
                            "Unsupported field in per_host_thresholds");
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

  EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml), EnvoyException,
                            "Didn't find a registered network or http filter or "
                            "protocol options implementation for name: 'no_such_filter'");
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
                            "Didn't find a registered network or http filter or "
                            "protocol options implementation for name: 'no_such_filter'");
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

TEST_F(ClusterInfoImplTest, DefaultConnectTimeout) {
  const std::string yaml = R"EOF(
  name: cluster1
  type: STRICT_DNS
  lb_policy: ROUND_ROBIN
)EOF";

  auto cluster = makeCluster(yaml);
  envoy::config::cluster::v3::Cluster cluster_config = parseClusterFromV3Yaml(yaml);

  EXPECT_FALSE(cluster_config.has_connect_timeout());
  EXPECT_EQ(std::chrono::seconds(5), cluster->info()->connectTimeout());
}

TEST_F(ClusterInfoImplTest, MaxConnectionDurationTest) {
  const std::string yaml_base = R"EOF(
  name: {}
  type: STRICT_DNS
  lb_policy: ROUND_ROBIN
  )EOF";

  const std::string yaml_set_max_connection_duration = yaml_base + R"EOF(
  typed_extension_protocol_options:
    envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
      "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
      explicit_http_config:
        http_protocol_options: {{}}
      common_http_protocol_options:
        max_connection_duration: {}
  )EOF";

  auto cluster1 = makeCluster(fmt::format(yaml_base, "cluster1"));
  EXPECT_EQ(absl::nullopt, cluster1->info()->maxConnectionDuration());

  auto cluster2 = makeCluster(fmt::format(yaml_set_max_connection_duration, "cluster2", "9s"));
  EXPECT_EQ(std::chrono::seconds(9), cluster2->info()->maxConnectionDuration());

  auto cluster3 = makeCluster(fmt::format(yaml_set_max_connection_duration, "cluster3", "0s"));
  EXPECT_EQ(absl::nullopt, cluster3->info()->maxConnectionDuration());
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

  const std::string explicit_timeout_new = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  const std::string explicit_timeout_bad = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  {
    auto cluster2 = makeCluster(yaml + explicit_timeout);
    ASSERT_TRUE(cluster2->info()->idleTimeout().has_value());
    EXPECT_EQ(std::chrono::seconds(1), cluster2->info()->idleTimeout().value());
  }
  {
    auto cluster2 = makeCluster(yaml + explicit_timeout_new);
    ASSERT_TRUE(cluster2->info()->idleTimeout().has_value());
    EXPECT_EQ(std::chrono::seconds(1), cluster2->info()->idleTimeout().value());
  }
  {
    auto cluster2 = makeCluster(yaml + explicit_timeout_new);
    EXPECT_THROW_WITH_REGEX(makeCluster(yaml + explicit_timeout_bad), EnvoyException,
                            ".*Proto constraint validation failed.*");
  }
  const std::string no_timeout = R"EOF(
    common_http_protocol_options:
      idle_timeout: 0s
  )EOF";

  const std::string no_timeout_new = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 0s
  )EOF";

  {
    auto cluster3 = makeCluster(yaml + no_timeout);
    EXPECT_FALSE(cluster3->info()->idleTimeout().has_value());
  }

  {
    auto cluster3 = makeCluster(yaml + no_timeout_new);
    EXPECT_FALSE(cluster3->info()->idleTimeout().has_value());
  }
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
    PANIC("not implemented");
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { PANIC("not implemented"); }
  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return parent_.createEmptyProtocolOptionsProto();
  }
  Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsConfig(const Protobuf::Message& msg,
                              Server::Configuration::ProtocolOptionsFactoryContext&) override {
    return parent_.createProtocolOptionsConfig(msg);
  }
  std::string name() const override { CONSTRUCT_ON_FIRST_USE(std::string, "envoy.test.filter"); }
  std::set<std::string> configTypes() override { return {}; };

  TestFilterConfigFactoryBase& parent_;
};

class TestHttpFilterConfigFactory : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  TestHttpFilterConfigFactory(TestFilterConfigFactoryBase& parent) : parent_(parent) {}

  // NamedNetworkFilterConfigFactory
  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::FactoryContext&) override {
    PANIC("not implemented");
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { PANIC("not implemented"); }
  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override { PANIC("not implemented"); }
  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig(const Protobuf::Message&,
                                  Server::Configuration::ServerFactoryContext&,
                                  ProtobufMessage::ValidationVisitor&) override {
    PANIC("not implemented");
  }

  ProtobufTypes::MessagePtr createEmptyProtocolOptionsProto() override {
    return parent_.createEmptyProtocolOptionsProto();
  }
  Upstream::ProtocolOptionsConfigConstSharedPtr
  createProtocolOptionsConfig(const Protobuf::Message& msg,
                              Server::Configuration::ProtocolOptionsFactoryContext&) override {
    return parent_.createProtocolOptionsConfig(msg);
  }
  std::string name() const override { CONSTRUCT_ON_FIRST_USE(std::string, "envoy.test.filter"); }
  std::set<std::string> configTypes() override { return {}; };

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

TEST_F(ClusterInfoImplTest, UseDownstreamHttpProtocolWithDowngrade) {
  const std::string yaml = R"EOF(
  name: name
  connect_timeout: 0.25s
  type: STRICT_DNS
  lb_policy: ROUND_ROBIN
  protocol_selection: USE_DOWNSTREAM_PROTOCOL
)EOF";

  auto cluster = makeCluster(yaml);

  EXPECT_EQ(Http::Protocol::Http10,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http10})[0]);
  EXPECT_EQ(Http::Protocol::Http11,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http11})[0]);
  EXPECT_EQ(Http::Protocol::Http2,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http2})[0]);
  // This will get downgraded because the cluster does not support HTTP/3
  EXPECT_EQ(Http::Protocol::Http2,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http3})[0]);
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

  EXPECT_EQ(Http::Protocol::Http2, cluster->info()->upstreamHttpProtocol(absl::nullopt)[0]);
  EXPECT_EQ(Http::Protocol::Http2,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http10})[0]);
  EXPECT_EQ(Http::Protocol::Http2,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http11})[0]);
  EXPECT_EQ(Http::Protocol::Http2,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http2})[0]);
}

TEST_F(ClusterInfoImplTest, UpstreamHttp11Protocol) {
  const std::string yaml = R"EOF(
  name: name
  connect_timeout: 0.25s
  type: STRICT_DNS
  lb_policy: ROUND_ROBIN
)EOF";

  auto cluster = makeCluster(yaml);

  EXPECT_EQ(Http::Protocol::Http11, cluster->info()->upstreamHttpProtocol(absl::nullopt)[0]);
  EXPECT_EQ(Http::Protocol::Http11,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http10})[0]);
  EXPECT_EQ(Http::Protocol::Http11,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http11})[0]);
  EXPECT_EQ(Http::Protocol::Http11,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http2})[0]);
}

#ifdef ENVOY_ENABLE_QUIC
TEST_F(ClusterInfoImplTest, Http3) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
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
    transport_socket:
      name: envoy.transport_sockets.quic
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport
        upstream_tls_context:
          common_tls_context:
            tls_certificates:
            - certificate_chain:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
              private_key:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
            validation_context:
              trusted_ca:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
              match_typed_subject_alt_names:
              - matcher:
                  exact: localhost
                san_type: URI
              - matcher:
                  exact: 127.0.0.1
                san_type: IP_ADDRESS
  )EOF",
                                                       Network::Address::IpVersion::v4);
  auto cluster1 = makeCluster(yaml);
  ASSERT_TRUE(cluster1->info()->idleTimeout().has_value());
  EXPECT_EQ(std::chrono::hours(1), cluster1->info()->idleTimeout().value());

  const std::string explicit_http3 = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http3_protocol_options:
            quic_protocol_options:
              max_concurrent_streams: 2
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  const std::string downstream_http3 = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        use_downstream_protocol_config:
          http3_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  auto explicit_h3 = makeCluster(yaml + explicit_http3);
  EXPECT_EQ(Http::Protocol::Http3,
            explicit_h3->info()->upstreamHttpProtocol({Http::Protocol::Http10})[0]);
  EXPECT_EQ(
      explicit_h3->info()->http3Options().quic_protocol_options().max_concurrent_streams().value(),
      2);

  auto downstream_h3 = makeCluster(yaml + downstream_http3);
  EXPECT_EQ(Http::Protocol::Http3,
            downstream_h3->info()->upstreamHttpProtocol({Http::Protocol::Http3})[0]);
  EXPECT_FALSE(
      downstream_h3->info()->http3Options().quic_protocol_options().has_max_concurrent_streams());
}

TEST_F(ClusterInfoImplTest, Http3BadConfig) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
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
    transport_socket:
      name: envoy.transport_sockets.not_quic
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport
        upstream_tls_context:
          common_tls_context:
            tls_certificates:
            - certificate_chain:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
              private_key:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
            validation_context:
              trusted_ca:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
              match_typed_subject_alt_names:
              - matcher:
                  exact: localhost
                san_type: URI
              - matcher:
                  exact: 127.0.0.1
                san_type: IP_ADDRESS
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        use_downstream_protocol_config:
          http3_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_REGEX(makeCluster(yaml), EnvoyException,
                          "HTTP3 requires a QuicUpstreamTransport transport socket: name.*");
}

TEST_F(ClusterInfoImplTest, Http3Auto) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
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
    transport_socket:
      name: envoy.transport_sockets.quic
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport
        upstream_tls_context:
          common_tls_context:
            tls_certificates:
            - certificate_chain:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
              private_key:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
            validation_context:
              trusted_ca:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
              match_typed_subject_alt_names:
              - matcher:
                  exact: localhost
                san_type: URI
              - matcher:
                  exact: 127.0.0.1
                san_type: IP_ADDRESS
  )EOF",
                                                       Network::Address::IpVersion::v4);

  auto cluster1 = makeCluster(yaml);
  ASSERT_TRUE(cluster1->info()->idleTimeout().has_value());
  EXPECT_EQ(std::chrono::hours(1), cluster1->info()->idleTimeout().value());

  const std::string auto_http3 = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        auto_config:
          http3_protocol_options:
            quic_protocol_options:
              max_concurrent_streams: 2
          alternate_protocols_cache_options:
            name: default
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  auto auto_h3 = makeCluster(yaml + auto_http3);
  EXPECT_EQ(Http::Protocol::Http3,
            auto_h3->info()->upstreamHttpProtocol({Http::Protocol::Http10})[0]);
  EXPECT_EQ(
      auto_h3->info()->http3Options().quic_protocol_options().max_concurrent_streams().value(), 2);
}

TEST_F(ClusterInfoImplTest, UseDownstreamHttpProtocolWithoutDowngrade) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
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
    transport_socket:
      name: envoy.transport_sockets.quic
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.quic.v3.QuicUpstreamTransport
        upstream_tls_context:
          common_tls_context:
            tls_certificates:
            - certificate_chain:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
              private_key:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
            validation_context:
              trusted_ca:
                filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
              match_typed_subject_alt_names:
              - matcher:
                  exact: localhost
                san_type: URI
              - matcher:
                  exact: 127.0.0.1
                san_type: IP_ADDRESS
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        use_downstream_protocol_config:
          http3_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF",
                                                       Network::Address::IpVersion::v4);
  auto cluster = makeCluster(yaml);

  EXPECT_EQ(Http::Protocol::Http10,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http10})[0]);
  EXPECT_EQ(Http::Protocol::Http11,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http11})[0]);
  EXPECT_EQ(Http::Protocol::Http2,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http2})[0]);
  EXPECT_EQ(Http::Protocol::Http3,
            cluster->info()->upstreamHttpProtocol({Http::Protocol::Http3})[0]);
}

#else
TEST_F(ClusterInfoImplTest, Http3BadConfig) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
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
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        use_downstream_protocol_config:
          http3_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF",
                                                       Network::Address::IpVersion::v4);

  EXPECT_THROW_WITH_REGEX(makeCluster(yaml), EnvoyException,
                          "HTTP3 configured but not enabled in the build.");
}
#endif // ENVOY_ENABLE_QUIC

TEST_F(ClusterInfoImplTest, Http2Auto) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
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
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
            match_typed_subject_alt_names:
            - matcher:
                exact: localhost
              san_type: URI
            - matcher:
                exact: 127.0.0.1
              san_type: IP_ADDRESS
  )EOF",
                                                       Network::Address::IpVersion::v4);

  auto cluster1 = makeCluster(yaml);
  ASSERT_TRUE(cluster1->info()->idleTimeout().has_value());
  EXPECT_EQ(std::chrono::hours(1), cluster1->info()->idleTimeout().value());

  const std::string auto_http2 = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        auto_config:
          http2_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  auto auto_h2 = makeCluster(yaml + auto_http2);
  EXPECT_EQ(Http::Protocol::Http2,
            auto_h2->info()->upstreamHttpProtocol({Http::Protocol::Http10})[0]);
}

TEST_F(ClusterInfoImplTest, Http2AutoNoAlpn) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
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
  )EOF",
                                                       Network::Address::IpVersion::v4);

  auto cluster1 = makeCluster(yaml);
  ASSERT_TRUE(cluster1->info()->idleTimeout().has_value());
  EXPECT_EQ(std::chrono::hours(1), cluster1->info()->idleTimeout().value());

  const std::string auto_http2 = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        auto_config:
          http2_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  EXPECT_THROW_WITH_REGEX(makeCluster(yaml + auto_http2), EnvoyException,
                          ".*which has a non-ALPN transport socket:.*");
}

TEST_F(ClusterInfoImplTest, Http2AutoWithNonAlpnMatcher) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
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
    transport_socket_matches:
    - match:
      name: insecure-mode
      transport_socket:
        name: envoy.transport_sockets.raw_buffer
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.raw_buffer.v3.RawBuffer
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
        common_tls_context:
          tls_certificates:
          - certificate_chain:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
            private_key:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
            match_typed_subject_alt_names:
            - matcher:
                exact: localhost
              san_type: URI
            - matcher:
                exact: 127.0.0.1
              san_type: IP_ADDRESS
  )EOF",
                                                       Network::Address::IpVersion::v4);

  auto cluster1 = makeCluster(yaml);
  ASSERT_TRUE(cluster1->info()->idleTimeout().has_value());
  EXPECT_EQ(std::chrono::hours(1), cluster1->info()->idleTimeout().value());

  const std::string auto_http2 = R"EOF(
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        auto_config:
          http2_protocol_options: {}
        common_http_protocol_options:
          idle_timeout: 1s
  )EOF";

  EXPECT_THROW_WITH_REGEX(makeCluster(yaml + auto_http2), EnvoyException,
                          ".*which has a non-ALPN transport socket matcher:.*");
}

// Validate empty singleton for HostsPerLocalityImpl.
TEST(HostsPerLocalityImpl, Empty) {
  EXPECT_FALSE(HostsPerLocalityImpl::empty()->hasLocalLocality());
  EXPECT_EQ(0, HostsPerLocalityImpl::empty()->get().size());
}

class HostsWithLocalityImpl : public Event::TestUsingSimulatedTime, public testing::Test {};

// Validate HostsPerLocalityImpl constructors.
TEST_F(HostsWithLocalityImpl, Cons) {
  {
    const HostsPerLocalityImpl hosts_per_locality;
    EXPECT_FALSE(hosts_per_locality.hasLocalLocality());
    EXPECT_EQ(0, hosts_per_locality.get().size());
  }

  MockClusterMockPrioritySet cluster;
  HostSharedPtr host_0 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 1);
  HostSharedPtr host_1 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 1);

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

TEST_F(HostsWithLocalityImpl, Filter) {
  MockClusterMockPrioritySet cluster;
  HostSharedPtr host_0 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 1);
  HostSharedPtr host_1 = makeTestHost(cluster.info_, "tcp://10.0.0.1:1234", simTime(), 1);

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

class HostSetImplLocalityTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  LocalityWeightsConstSharedPtr locality_weights_;
  HostSetImpl host_set_{0, kDefaultOverProvisioningFactor};
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  HostVector hosts_{makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                    makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                    makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                    makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
                    makeTestHost(info_, "tcp://127.0.0.1:84", simTime()),
                    makeTestHost(info_, "tcp://127.0.0.1:85", simTime())};
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
    auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
    HostVector hosts{makeTestHost(cluster_info, "tcp://127.0.0.1:80", *time_source),
                     makeTestHost(cluster_info, "tcp://127.0.0.1:81", *time_source),
                     makeTestHost(cluster_info, "tcp://127.0.0.1:82", *time_source)};
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
  auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
  HostVector hosts{makeTestHost(info, "tcp://127.0.0.1:80", *time_source),
                   makeTestHost(info, "tcp://127.0.0.1:81", *time_source),
                   makeTestHost(info, "tcp://127.0.0.1:82", *time_source),
                   makeTestHost(info, "tcp://127.0.0.1:83", *time_source),
                   makeTestHost(info, "tcp://127.0.0.1:84", *time_source)};

  hosts[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  hosts[1]->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  hosts[2]->healthFlagSet(Host::HealthFlag::PENDING_ACTIVE_HC);
  hosts[2]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  hosts[4]->healthFlagSet(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL);
  hosts[4]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);

  auto hosts_per_locality =
      makeHostsPerLocality({{hosts[0], hosts[1]}, {hosts[2], hosts[3], hosts[4]}});

  auto update_hosts_params =
      HostSetImpl::partitionHosts(std::make_shared<const HostVector>(hosts), hosts_per_locality);

  EXPECT_EQ(5, update_hosts_params.hosts->size());
  EXPECT_EQ(1, update_hosts_params.healthy_hosts->get().size());
  EXPECT_EQ(hosts[3], update_hosts_params.healthy_hosts->get()[0]);
  EXPECT_EQ(1, update_hosts_params.degraded_hosts->get().size());
  EXPECT_EQ(hosts[1], update_hosts_params.degraded_hosts->get()[0]);
  EXPECT_EQ(2, update_hosts_params.excluded_hosts->get().size());
  EXPECT_EQ(hosts[2], update_hosts_params.excluded_hosts->get()[0]);
  EXPECT_EQ(hosts[4], update_hosts_params.excluded_hosts->get()[1]);

  EXPECT_EQ(2, update_hosts_params.hosts_per_locality->get()[0].size());
  EXPECT_EQ(3, update_hosts_params.hosts_per_locality->get()[1].size());

  EXPECT_EQ(0, update_hosts_params.healthy_hosts_per_locality->get()[0].size());
  EXPECT_EQ(1, update_hosts_params.healthy_hosts_per_locality->get()[1].size());
  EXPECT_EQ(hosts[3], update_hosts_params.healthy_hosts_per_locality->get()[1][0]);

  EXPECT_EQ(1, update_hosts_params.degraded_hosts_per_locality->get()[0].size());
  EXPECT_EQ(0, update_hosts_params.degraded_hosts_per_locality->get()[1].size());
  EXPECT_EQ(hosts[1], update_hosts_params.degraded_hosts_per_locality->get()[0][0]);

  EXPECT_EQ(0, update_hosts_params.excluded_hosts_per_locality->get()[0].size());
  EXPECT_EQ(2, update_hosts_params.excluded_hosts_per_locality->get()[1].size());
  EXPECT_EQ(hosts[2], update_hosts_params.excluded_hosts_per_locality->get()[1][0]);
  EXPECT_EQ(hosts[4], update_hosts_params.excluded_hosts_per_locality->get()[1][1]);
}

TEST_F(ClusterInfoImplTest, MaxRequestsPerConnectionValidation) {
  const std::string yaml = R"EOF(
  name: cluster1
  type: STRICT_DNS
  lb_policy: ROUND_ROBIN
  max_requests_per_connection: 3
  typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        common_http_protocol_options:
          max_requests_per_connection: 3
        use_downstream_protocol_config: {}
)EOF";

  EXPECT_THROW_WITH_MESSAGE(makeCluster(yaml), EnvoyException,
                            "Only one of max_requests_per_connection from Cluster or "
                            "HttpProtocolOptions can be specified");
}

TEST_F(ClusterInfoImplTest, DeprecatedMaxRequestsPerConnection) {
  const std::string yaml = R"EOF(
  name: cluster1
  type: STRICT_DNS
  lb_policy: ROUND_ROBIN
  max_requests_per_connection: 3
)EOF";

  auto cluster = makeCluster(yaml);

  EXPECT_EQ(3U, cluster->info()->maxRequestsPerConnection());
}

TEST_F(ClusterInfoImplTest, FilterChain) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
    name: name
    connect_timeout: 0.25s
    type: STRICT_DNS
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        explicit_http_config:
          http2_protocol_options: {}
  )EOF",
                                                       Network::Address::IpVersion::v4);

  auto cluster = makeCluster(yaml);
  Http::MockFilterChainManager manager;
  EXPECT_FALSE(cluster->info()->createUpgradeFilterChain("foo", nullptr, manager));

  EXPECT_CALL(manager, applyFilterFactoryCb(_, _));
  cluster->info()->createFilterChain(manager);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
