#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/stats/scope.h"

#include "common/network/utility.h"
#include "common/upstream/logical_dns_cluster.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Upstream {

enum class ConfigType { V2_YAML, V1_JSON };

class LogicalDnsClusterTest : public testing::Test {
public:
  void setupFromV1Json(const std::string& json) {
    resolve_timer_ = new Event::MockTimer(&dispatcher_);
    NiceMock<MockClusterManager> cm;
    envoy::api::v2::Cluster cluster_config = parseClusterFromJson(json);
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
        "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        ssl_context_manager_, *scope, cm, local_info_, dispatcher_, random_, stats_store_);
    cluster_.reset(new LogicalDnsCluster(cluster_config, runtime_, dns_resolver_, tls_,
                                         factory_context, std::move(scope), false));
    cluster_->prioritySet().addMemberUpdateCb(
        [&](uint32_t, const HostVector&, const HostVector&) -> void {
          membership_updated_.ready();
        });
    cluster_->initialize([&]() -> void { initialized_.ready(); });
  }

  void setupFromV2Yaml(const std::string& yaml) {
    resolve_timer_ = new Event::MockTimer(&dispatcher_);
    NiceMock<MockClusterManager> cm;
    envoy::api::v2::Cluster cluster_config = parseClusterFromV2Yaml(yaml);
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
        "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        ssl_context_manager_, *scope, cm, local_info_, dispatcher_, random_, stats_store_);
    cluster_.reset(new LogicalDnsCluster(cluster_config, runtime_, dns_resolver_, tls_,
                                         factory_context, std::move(scope), false));
    cluster_->prioritySet().addMemberUpdateCb(
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
                      ConfigType config_type = ConfigType::V2_YAML) {
    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    if (config_type == ConfigType::V1_JSON) {
      setupFromV1Json(config);
    } else {
      setupFromV2Yaml(config);
    }

    EXPECT_CALL(membership_updated_, ready());
    EXPECT_CALL(initialized_, ready());
    EXPECT_CALL(*resolve_timer_, enableTimer(std::chrono::milliseconds(4000)));
    dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2"}));

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

    EXPECT_CALL(dispatcher_,
                createClientConnection_(
                    PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443")), _, _, _))
        .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
    logical_host->createConnection(dispatcher_, nullptr);
    logical_host->outlierDetector().putHttpResponseCode(200);

    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    resolve_timer_->callback_();

    // Should not cause any changes.
    EXPECT_CALL(*resolve_timer_, enableTimer(_));
    dns_callback_(TestUtility::makeDnsResponse({"127.0.0.1", "127.0.0.2", "127.0.0.3"}));

    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
    EXPECT_CALL(dispatcher_,
                createClientConnection_(
                    PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.1:443")), _, _, _))
        .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
    Host::CreateConnectionData data = logical_host->createConnection(dispatcher_, nullptr);
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
    EXPECT_TRUE(TestUtility::protoEqual(envoy::api::v2::core::Metadata::default_instance(),
                                        *data.host_description_->metadata()));
    data.host_description_->outlierDetector().putHttpResponseCode(200);
    data.host_description_->healthChecker().setUnhealthy();

    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    resolve_timer_->callback_();

    // Should cause a change.
    EXPECT_CALL(*resolve_timer_, enableTimer(_));
    dns_callback_(TestUtility::makeDnsResponse({"127.0.0.3", "127.0.0.1", "127.0.0.2"}));

    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
    EXPECT_CALL(dispatcher_,
                createClientConnection_(
                    PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443")), _, _, _))
        .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
    logical_host->createConnection(dispatcher_, nullptr);

    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    resolve_timer_->callback_();

    // Empty should not cause any change.
    EXPECT_CALL(*resolve_timer_, enableTimer(_));
    dns_callback_({});

    EXPECT_EQ(logical_host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
    EXPECT_CALL(dispatcher_,
                createClientConnection_(
                    PointeesEq(Network::Utility::resolveUrl("tcp://127.0.0.3:443")), _, _, _))
        .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
    logical_host->createConnection(dispatcher_, nullptr);

    // Make sure we cancel.
    EXPECT_CALL(active_dns_query_, cancel());
    expectResolve(Network::DnsLookupFamily::V4Only, expected_address);
    resolve_timer_->callback_();

    tls_.shutdownThread();
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  std::shared_ptr<NiceMock<Network::MockDnsResolver>> dns_resolver_{
      new NiceMock<Network::MockDnsResolver>};
  Network::MockActiveDnsQuery active_dns_query_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Network::DnsResolver::ResolveCb dns_callback_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Event::MockTimer* resolve_timer_;
  std::shared_ptr<LogicalDnsCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

typedef std::tuple<std::string, Network::DnsLookupFamily, std::list<std::string>>
    LogicalDnsConfigTuple;
std::vector<LogicalDnsConfigTuple> generateLogicalDnsParams() {
  std::vector<LogicalDnsConfigTuple> dns_config;
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
    std::list<std::string> dns_response{"::1"};
    dns_config.push_back(std::make_tuple(family_json, family, dns_response));
  }
  return dns_config;
}

class LogicalDnsParamTest : public LogicalDnsClusterTest,
                            public testing::WithParamInterface<LogicalDnsConfigTuple> {};

INSTANTIATE_TEST_CASE_P(DnsParam, LogicalDnsParamTest,
                        testing::ValuesIn(generateLogicalDnsParams()));

// Validate that if the DNS resolves immediately, during the LogicalDnsCluster
// constructor, we have the expected host state and initialization callback
// invocation.
TEST_P(LogicalDnsParamTest, ImmediateResolve) {
  const std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "logical_dns",
    "lb_type": "round_robin",
  )EOF" + std::get<0>(GetParam()) +
                           R"EOF(
    "hosts": [{"url": "tcp://foo.bar.com:443"}]
  }
  )EOF";

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*dns_resolver_, resolve("foo.bar.com", std::get<1>(GetParam()), _))
      .WillOnce(Invoke([&](const std::string&, Network::DnsLookupFamily,
                           Network::DnsResolver::ResolveCb cb) -> Network::ActiveDnsQuery* {
        EXPECT_CALL(*resolve_timer_, enableTimer(_));
        cb(TestUtility::makeDnsResponse(std::get<2>(GetParam())));
        return nullptr;
      }));
  setupFromV1Json(json);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ("foo.bar.com",
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());
  cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthChecker().setUnhealthy();
  tls_.shutdownThread();
}

TEST_F(LogicalDnsClusterTest, BadConfig) {
  const std::string multiple_hosts_json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "logical_dns",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://foo.bar.com:443"}, {"url": "tcp://foo2.bar.com:443"}]
  }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromV1Json(multiple_hosts_json), EnvoyException,
                            "LOGICAL_DNS clusters must have a single host");

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
      setupFromV2Yaml(multiple_lb_endpoints_yaml), EnvoyException,
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
      setupFromV2Yaml(multiple_endpoints_yaml), EnvoyException,
      "LOGICAL_DNS clusters must have a single locality_lb_endpoint and a single lb_endpoint");
}

TEST_F(LogicalDnsClusterTest, Basic) {
  const std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "logical_dns",
    "lb_type": "round_robin",
    "hosts": [{"url": "tcp://foo.bar.com:443"}],
    "dns_refresh_rate_ms": 4000
  }
  )EOF";

  const std::string basic_yaml_hosts = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
  connect_timeout: 0.25s
  lb_policy: ROUND_ROBIN
  # Since the following expectResolve() requires Network::DnsLookupFamily::V4Only we need to set
  # dns_lookup_family to V4_ONLY explicitly for v2 .yaml config.
  dns_lookup_family: V4_ONLY
  hosts:
  - socket_address:
      address: foo.bar.com
      port_value: 443
  )EOF";

  const std::string basic_yaml_load_assignment = R"EOF(
  name: name
  type: LOGICAL_DNS
  dns_refresh_rate: 4s
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

  testBasicSetup(json, "foo.bar.com", ConfigType::V1_JSON);
  testBasicSetup(basic_yaml_hosts, "foo.bar.com");
  testBasicSetup(basic_yaml_load_assignment, "foo.bar.com");
}

} // namespace Upstream
} // namespace Envoy
