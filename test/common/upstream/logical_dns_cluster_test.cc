#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/stats/scope.h"

#include "common/network/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/logical_dns_cluster.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

class LogicalDnsClusterTest : public testing::Test {
protected:
  LogicalDnsClusterTest() : api_(Api::createApiForTest(stats_store_)) {}

  void setupFromV3Yaml(const std::string& yaml, bool avoid_boosting = true) {
    resolve_timer_ = new Event::MockTimer(&dispatcher_);
    NiceMock<MockClusterManager> cm;
    envoy::config::cluster::v3::Cluster cluster_config =
        parseClusterFromV3Yaml(yaml, avoid_boosting);
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
        "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm, local_info_, dispatcher_, random_, stats_store_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    cluster_ = std::make_shared<LogicalDnsCluster>(cluster_config, runtime_, dns_resolver_,
                                                   factory_context, std::move(scope), false);
    cluster_->prioritySet().addPriorityUpdateCb(
        [&](uint32_t, const HostVector&, const HostVector&) -> void {
          membership_updated_.ready();
        });
    cluster_->initialize([&]() -> void { initialized_.ready(); });
  }

  void expectResolve(Network::DnsLookupFamily dns_lookup_family,
                     const std::string& expected_address) {
    EXPECT_CALL(*dns_resolver_, resolve(expected_address, dns_lookup_family, _))
        .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                             Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
          dns_callback_ = cb;
          return &active_dns_query_;
        }));
  }

  void testBasicSetup(const std::string& config, const std::string& expected_address,
                      uint32_t expected_port, uint32_t expected_hc_port) {
    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    setupFromV3Yaml(config);

    EXPECT_CALL(membership_updated_, ready());
    EXPECT_CALL(initialized_, ready());
    EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(4000), _));
    dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                  TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

    EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
    EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
    EXPECT_EQ(1UL,
              cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
    EXPECT_EQ(
        1UL,
        cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
    EXPECT_EQ(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0],
              cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts()[0]);
    HostSharedPtr logical_host = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0];

    EXPECT_EQ("127.0.0.1:" + std::to_string(expected_hc_port),
              logical_host->healthCheckAddress()->asString());
    EXPECT_EQ("127.0.0.1:" + std::to_string(expected_port), logical_host->address()->asString());

    EXPECT_CALL(dispatcher_,
                createClientConnection_(
                    PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443")), _, _, _))
        .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
    logical_host->createConnection(dispatcher_, nullptr, nullptr);
    logical_host->outlierDetector().putHttpResponseCode(200);

    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    resolve_timer_->invokeCallback();

    // Should not cause any changes.
    EXPECT_CALL(*resolve_timer_, enableTimer(_, _));
    dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                  TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2", "127.0.0.3"}));

    EXPECT_EQ("127.0.0.1:" + std::to_string(expected_hc_port),
              logical_host->healthCheckAddress()->asString());
    EXPECT_EQ("127.0.0.1:" + std::to_string(expected_port), logical_host->address()->asString());

    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
    EXPECT_CALL(dispatcher_,
                createClientConnection_(
                    PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443")), _, _, _))
        .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
    Host::CreateConnectionData data = logical_host->createConnection(dispatcher_, nullptr, nullptr);
    EXPECT_FALSE(data.host_description_->canary());
    EXPECT_EQ(&cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->cluster(),
              &data.host_description_->cluster());
    EXPECT_EQ(&cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->stats(),
              &data.host_description_->stats());
    EXPECT_EQ("127.0.0.1:443", data.host_description_->address()->asString());
    EXPECT_EQ("", data.host_description_->locality().region());
    EXPECT_EQ("", data.host_description_->locality().zone());
    EXPECT_EQ("", data.host_description_->locality().sub_zone());
    EXPECT_EQ("foo.bar.com", data.host_description_->hostname());
    EXPECT_TRUE(TestUtility::protoEqual(envoy::config::core::v3::Metadata::default_instance(),
                                        *data.host_description_->metadata()));
    data.host_description_->outlierDetector().putHttpResponseCode(200);
    data.host_description_->healthChecker().setUnhealthy();

    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    resolve_timer_->invokeCallback();

    // Should cause a change.
    EXPECT_CALL(*resolve_timer_, enableTimer(_, _));
    dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                  TestUtility::makeDnsResponse({"127.0.0.3", "127.0.0.1", "127.0.0.2"}));

    EXPECT_EQ("127.0.0.3:" + std::to_string(expected_hc_port),
              logical_host->healthCheckAddress()->asString());
    EXPECT_EQ("127.0.0.3:" + std::to_string(expected_port), logical_host->address()->asString());

    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
    EXPECT_CALL(dispatcher_,
                createClientConnection_(
                    PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443")), _, _, _))
        .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
    logical_host->createConnection(dispatcher_, nullptr, nullptr);

    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    resolve_timer_->invokeCallback();

    // Failure should not cause any change.
    ON_CALL(random_, random()).WillByDefault(Return(6000));
    EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(6000), _));
    dns_callback_(Network::DnsResolver::ResolutionStatus::Failure, {});

    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
    EXPECT_CALL(dispatcher_,
                createClientConnection_(
                    PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443")), _, _, _))
        .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
    logical_host->createConnection(dispatcher_, nullptr, nullptr);

    // Empty Success should not cause any change.
    ON_CALL(random_, random()).WillByDefault(Return(6000));
    EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(6000), _));
    dns_callback_(Network::DnsResolver::ResolutionStatus::Success, {});

    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
    EXPECT_CALL(dispatcher_,
                createClientConnection_(
                    PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443")), _, _, _))
        .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
    logical_host->createConnection(dispatcher_, nullptr, nullptr);

    // Make sure we cancel.
    EXPECT_CALL(active_dns_query_, cancel());
    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    resolve_timer_->invokeCallback();

    tls_.shutdownThread();
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  Network::MockActiveDnsQuery active_dns_query_;
  NiceMock<Random::MockRandomGenerator> random_;
  Network::DnsResolver::ResolveCb dns_callback_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Event::MockTimer* resolve_timer_;
  std::shared_ptr<LogicalDnsCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
};

using LogicalDnsConfigTuple =
    std::tuple<std::string, Network::DnsLookupFamily, std::list<std::string>>;
std::vector<LogicalDnsConfigTuple> generateLogicalDnsParams() {
  std::vector<LogicalDnsConfigTuple> dns_config;
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
    std::list<std::string> dns_response{"::1"};
    dns_config.push_back(std::make_tuple(family_yaml, family, dns_response));
  }
  return dns_config;
}

class LogicalDnsParamTest : public LogicalDnsClusterTest,
                            public testing::WithParamInterface<LogicalDnsConfigTuple> {};

INSTANTIATE_TEST_SUITE_P(DnsParam, LogicalDnsParamTest,
                         testing::ValuesIn(generateLogicalDnsParams()));

// Validate that if the DNS resolves immediately, during the LogicalDnsCluster
// constructor, we have the expected host state and initialization callback
// invocation.
TEST_P(LogicalDnsParamTest, ImmediateResolve) {
  const std::string yaml = R"EOF(
  name: name
  connect_timeout: 0.25s
  type: logical_dns
  lb_policy: round_robin
  )EOF" + std::get<0>(GetParam()) +
                           R"EOF(
  load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
  )EOF";

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*dns_resolver_, resolve("foo.bar.com", std::get<1>(GetParam()), _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        EXPECT_CALL(*resolve_timer_, enableTimer(_, _));
        cb(Network::DnsResolver::ResolutionStatus::Success,
           TestUtility::makeDnsResponse(std::get<2>(GetParam())));
        return nullptr;
      }));
  setupFromV3Yaml(yaml);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("foo.bar.com",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthChecker().setUnhealthy();
  tls_.shutdownThread();
}

TEST_F(LogicalDnsParamTest, FailureRefreshRateBackoffResetsWhenSuccessHappens) {
  const std::string yaml = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
  dns_failure_refresh_rate:
    base_interval: 7s
    max_interval: 10s
  connect_timeout: 0.25s
  lb_policy: ROUND_ROBIN
  # Since the following expectResolve() requires Network::DnsLookupFamily::V4Only we need to set
  # dns_lookup_family to V4_ONLY explicitly for v2 .yaml config.
  dns_lookup_family: V4_ONLY
  load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
  )EOF";

  expectResolve(Network::DnsLookupFamily::V4Only, "foo.bar.com");
  setupFromV3Yaml(yaml);

  // Failing response kicks the failure refresh backoff strategy.
  ON_CALL(random_, random()).WillByDefault(Return(8000));
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  dns_callback_(Network::DnsResolver::ResolutionStatus::Failure, {});

  // Successful call should reset the failure backoff strategy.
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(4000), _));
  dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

  // Therefore, a subsequent failure should get a [0,base * 1] refresh.
  ON_CALL(random_, random()).WillByDefault(Return(8000));
  EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  dns_callback_(Network::DnsResolver::ResolutionStatus::Failure, {});

  tls_.shutdownThread();
}

TEST_F(LogicalDnsParamTest, TtlAsDnsRefreshRate) {
  const std::string yaml = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
  respect_dns_ttl: true
  connect_timeout: 0.25s
  lb_policy: ROUND_ROBIN
  # Since the following expectResolve() requires Network::DnsLookupFamily::V4Only we need to set
  # dns_lookup_family to V4_ONLY explicitly for v2 .yaml config.
  dns_lookup_family: V4_ONLY
  load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                     address: foo.bar.com
                     port_value: 443
  )EOF";

  expectResolve(Network::DnsLookupFamily::V4Only, "foo.bar.com");
  setupFromV3Yaml(yaml);

  // TTL is recorded when the DNS response is successful and not empty
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(5000), _));
  dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}, std::chrono::seconds(5)));

  // If the response is successful but empty, the cluster uses the cluster configured refresh rate.
  EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(4000), _));
  dns_callback_(Network::DnsResolver::ResolutionStatus::Success,
                TestUtility::makeDnsResponse({}, std::chrono::seconds(5)));

  // On failure, the cluster uses the cluster configured refresh rate.
  EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(4000), _));
  dns_callback_(Network::DnsResolver::ResolutionStatus::Failure,
                TestUtility::makeDnsResponse({}, std::chrono::seconds(5)));

  tls_.shutdownThread();
}

TEST_F(LogicalDnsClusterTest, BadConfig) {
  const std::string multiple_hosts_yaml = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
  connect_timeout: 0.25s
  lb_policy: ROUND_ROBIN
  load_assignment:
        cluster_name: name
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443                     
            - endpoint:
                address:
                  socket_address:
                    address: foo2.bar.com
                    port_value: 443
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      setupFromV3Yaml(multiple_hosts_yaml), EnvoyException,
      "LOGICAL_DNS clusters must have a single locality_lb_endpoint and a single lb_endpoint");

  const std::string multiple_lb_endpoints_yaml = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
  connect_timeout: 0.25s
  lb_policy: ROUND_ROBIN
  dns_lookup_family: V4_ONLY
  load_assignment:
    cluster_name: name
    endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: foo.bar.com
                port_value: 443
            health_check_config:
              port_value: 8000
        - endpoint:
            address:
              socket_address:
                address: hello.world.com
                port_value: 443
            health_check_config:
              port_value: 8000
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      setupFromV3Yaml(multiple_lb_endpoints_yaml), EnvoyException,
      "LOGICAL_DNS clusters must have a single locality_lb_endpoint and a single lb_endpoint");

  const std::string multiple_endpoints_yaml = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
  connect_timeout: 0.25s
  lb_policy: ROUND_ROBIN
  dns_lookup_family: V4_ONLY
  load_assignment:
    cluster_name: name
    endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: foo.bar.com
                port_value: 443
            health_check_config:
              port_value: 8000

      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: hello.world.com
                port_value: 443
            health_check_config:
              port_value: 8000
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      setupFromV3Yaml(multiple_endpoints_yaml), EnvoyException,
      "LOGICAL_DNS clusters must have a single locality_lb_endpoint and a single lb_endpoint");

  const std::string custom_resolver_yaml = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
  connect_timeout: 0.25s
  lb_policy: ROUND_ROBIN
  dns_lookup_family: V4_ONLY
  load_assignment:
    cluster_name: name
    endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: hello.world.com
                port_value: 443
                resolver_name: customresolver
            health_check_config:
              port_value: 8000
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromV3Yaml(custom_resolver_yaml), EnvoyException,
                            "LOGICAL_DNS clusters must NOT have a custom resolver name set");
}

TEST_F(LogicalDnsClusterTest, Basic) {
  const std::string basic_yaml_hosts = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
  dns_failure_refresh_rate:
    base_interval: 7s
    max_interval: 10s
  connect_timeout: 0.25s
  lb_policy: ROUND_ROBIN
  # Since the following expectResolve() requires Network::DnsLookupFamily::V4Only we need to set
  # dns_lookup_family to V4_ONLY explicitly for v2 .yaml config.
  dns_lookup_family: V4_ONLY
  load_assignment:
        endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: foo.bar.com
                    port_value: 443
  )EOF";

  const std::string basic_yaml_load_assignment = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
  dns_failure_refresh_rate:
    base_interval: 7s
    max_interval: 10s
  connect_timeout: 0.25s
  lb_policy: ROUND_ROBIN
  # Since the following expectResolve() requires Network::DnsLookupFamily::V4Only we need to set
  # dns_lookup_family to V4_ONLY explicitly for v2 .yaml config.
  dns_lookup_family: V4_ONLY
  load_assignment:
    cluster_name: name
    endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: foo.bar.com
                port_value: 443
            health_check_config:
              port_value: 8000
  )EOF";

  testBasicSetup(basic_yaml_hosts, "foo.bar.com", 443, 443);
  // Expect to override the health check address port value.
  testBasicSetup(basic_yaml_load_assignment, "foo.bar.com", 443, 8000);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
