#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/stats/scope.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/singleton/manager_impl.h"
#include "common/upstream/original_dst_cluster.h"
#include "common/upstream/upstream_impl.h"

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
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace OriginalDst {

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  TestLoadBalancerContext(const Network::Connection* connection) : connection_(connection) {}
  TestLoadBalancerContext(const Network::Connection* connection, const std::string& key,
                          const std::string& value)
      : connection_(connection) {
    downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{key, value}}};
  }

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return 0; }
  const Network::Connection* downstreamConnection() const override { return connection_; }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return downstream_headers_.get();
  }

  absl::optional<uint64_t> hash_key_;
  const Network::Connection* connection_;
  Http::RequestHeaderMapPtr downstream_headers_;
};

class ClusterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  // cleanup timer must be created before the cluster (in setup()), so that we can set expectations
  // on it. Ownership is transferred to the cluster at the cluster constructor, so the cluster will
  // take care of destructing it!
  ClusterTest()
      : cleanup_timer_(new Event::MockTimer(&dispatcher_)),
        api_(Api::createApiForTest(stats_store_)) {}

  void setupFromYaml(const std::string& yaml, bool uses_tls, bool avoid_boosting = true) {
    const envoy::config::cluster::v3::Cluster& cluster_config = parseClusterFromV3Yaml(yaml, avoid_boosting);

    NiceMock<MockClusterManager> cm;
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
        "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        admin_, ssl_context_manager_, *scope, cm, local_info_, dispatcher_, stats_store_,
        singleton_manager_, tls_, validation_visitor_, *api_);
    if (uses_tls) {
      EXPECT_CALL(ssl_context_manager_, createSslClientContext(_, _, _));
    }

    cluster_ = std::make_shared<Cluster>(cluster_config, runtime_, factory_context,
                                         std::move(scope), false);
    thread_aware_lb_ = std::make_unique<Cluster::ThreadAwareLoadBalancer>(cluster_);
    lb_factory_ = thread_aware_lb_->factory();
    refreshLb();

    cluster_->prioritySet().addPriorityUpdateCb(
        [&](uint32_t, const HostVector& hosts_added, const HostVector& hosts_removed) -> void {
          // Mock Cluster Manager by refreshing the load balancer when membership has changed
          if (hosts_added.size() || hosts_removed.size()) {
            ENVOY_LOG_MISC(debug, "Refreshing LoadBalancer");
            refreshLb();
          }
          membership_updated_.ready();
        });
    cluster_->initialize([&]() -> void { initialized_.ready(); });
  }

  void refreshLb() { lb_ = lb_factory_->create(); }

  const Network::TransportSocketOptionsSharedPtr
  getDefaultTransportSocketOptions(const Cluster::Host* host) {
    return host->default_transport_socket_options_;
  }

  Stats::TestUtil::TestStore stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  ClusterSharedPtr cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* cleanup_timer_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Server::MockAdmin> admin_;
  Singleton::ManagerImpl singleton_manager_{Thread::threadFactoryForTest()};
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Api::ApiPtr api_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr lb_factory_;
  Upstream::LoadBalancerPtr lb_;

  const std::string default_yaml_ = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: original_dst
    lb_policy: cluster_provided
  )EOF";

  const std::string default_tls_yaml_ = default_yaml_ + TestEnvironment::substitute(R"EOF(
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
        common_tls_context:
          validation_context:
            trusted_ca:
              filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
  )EOF");
};

TEST(ClusterConfigTest, GoodConfig) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: original_dst
    lb_policy: cluster_provided
    cleanup_interval: 1s
  )EOF";

  EXPECT_TRUE(parseClusterFromV3Yaml(yaml).has_cleanup_interval());
}

TEST_F(ClusterTest, BadConfigWithLoadAssignment) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    load_assignment:
      cluster_name: name
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 8000
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      setupFromYaml(yaml, false), EnvoyException,
      "ORIGINAL_DST clusters must have no load assignment or hosts configured");
}

TEST_F(ClusterTest, DEPRECATED_FEATURE_TEST(BadConfigWithDeprecatedHosts)) {
  TestDeprecatedV2Api _deprecated_v2_api;
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: ORIGINAL_DST
    lb_policy: ORIGINAL_DST_LB
    hosts:
      - socket_address:
          address: 127.0.0.1
          port_value: 8000
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      setupFromYaml(yaml, false, false), EnvoyException,
      "ORIGINAL_DST clusters must have no load assignment or hosts configured");
}

TEST_F(ClusterTest, CleanupInterval) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
  )EOF"; // Help Emacs balance quotation marks: "

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(*cleanup_timer_, enableTimer(std::chrono::milliseconds(1000), _));
  setupFromYaml(yaml, false);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

TEST_F(ClusterTest, NoContext) {
  std::string yaml = R"EOF(
    name: name,
    connect_timeout: 0.125s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(yaml, false);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // No downstream connection => no host.
  {
    TestLoadBalancerContext lb_context(nullptr);
    EXPECT_CALL(dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb_->chooseHost(&lb_context);
    EXPECT_EQ(host, nullptr);
  }

  // Downstream connection is not using original dst => no host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);

    EXPECT_CALL(dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb_->chooseHost(&lb_context);
    EXPECT_EQ(host, nullptr);
  }

  // No host for non-IP address
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    connection.stream_info_.downstream_address_provider_->restoreLocalAddress(
        std::make_shared<Network::Address::PipeInstance>("unix://foo"));

    EXPECT_CALL(dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb_->chooseHost(&lb_context);
    EXPECT_EQ(host, nullptr);
  }
}

TEST_F(ClusterTest, Membership) {
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(default_yaml_, false);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  EXPECT_CALL(membership_updated_, ready());

  // Host gets the local address of the downstream connection.

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb_->chooseHost(&lb_context);
  post_cb();
  auto cluster_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();

  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.addressProvider().localAddress(), *host->address());

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  EXPECT_EQ(host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(*connection.addressProvider().localAddress(),
            *cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address());

  // Same host is returned on the 2nd call
  HostConstSharedPtr host2 = lb_->chooseHost(&lb_context);
  EXPECT_EQ(host2, host);

  // Make host time out, no membership changes happen on the first timeout.
  ASSERT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(true, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->used());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(
      cluster_hosts,
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector remains the same

  // host gets removed on the 2nd timeout.
  ASSERT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(false, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->used());

  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->invokeCallback();
  EXPECT_NE(cluster_hosts,
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector changes

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  cluster_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();

  // New host gets created
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host3 = lb_->chooseHost(&lb_context);
  post_cb();
  EXPECT_NE(host3, nullptr);
  EXPECT_NE(host3, host);
  EXPECT_NE(cluster_hosts,
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector changes

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(host3, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
}

TEST_F(ClusterTest, Membership2) {
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(default_yaml_, false);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // Host gets the local address of the downstream connection.

  NiceMock<Network::MockConnection> connection1;
  TestLoadBalancerContext lb_context1(&connection1);
  connection1.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  NiceMock<Network::MockConnection> connection2;
  TestLoadBalancerContext lb_context2(&connection2);
  connection2.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.12"));

  EXPECT_CALL(membership_updated_, ready());
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host1 = lb_->chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ(*connection1.addressProvider().localAddress(), *host1->address());

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host2 = lb_->chooseHost(&lb_context2);
  post_cb();
  ASSERT_NE(host2, nullptr);
  EXPECT_EQ(*connection2.addressProvider().localAddress(), *host2->address());

  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  EXPECT_EQ(host1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(*connection1.addressProvider().localAddress(),
            *cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address());

  EXPECT_EQ(host2, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]);
  EXPECT_EQ(*connection2.addressProvider().localAddress(),
            *cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]->address());

  auto cluster_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();

  // Make hosts time out, no membership changes happen on the first timeout.
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(true, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->used());
  EXPECT_EQ(true, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]->used());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(
      cluster_hosts,
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector remains the same

  // both hosts get removed on the 2nd timeout.
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(false, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->used());
  EXPECT_EQ(false, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]->used());

  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->invokeCallback();
  EXPECT_NE(cluster_hosts,
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector changes

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(ClusterTest, Connection) {
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(default_yaml_, false);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  EXPECT_CALL(membership_updated_, ready());

  // Connection to the host is made to the downstream connection's local address.
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("FD00::1"));

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb_->chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.addressProvider().localAddress(), *host->address());

  EXPECT_CALL(dispatcher_, createClientConnection_(
                               PointeesEq(connection.addressProvider().localAddress()), _, _, _))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  host->createConnection(dispatcher_, nullptr, nullptr);
}

TEST_F(ClusterTest, MultipleClusters) {
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(default_yaml_, false);

  PrioritySetImpl second;
  cluster_->prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector& added, const HostVector& removed) -> void {
        // Update second hostset accordingly;
        HostVectorSharedPtr new_hosts(
            new HostVector(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()));
        auto healthy_hosts = std::make_shared<const HealthyHostVector>(
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts());
        const HostsPerLocalityConstSharedPtr empty_hosts_per_locality{new HostsPerLocalityImpl()};

        second.updateHosts(0,
                           updateHostsParams(new_hosts, empty_hosts_per_locality, healthy_hosts,
                                             empty_hosts_per_locality),
                           {}, added, removed, absl::nullopt);
      });

  EXPECT_CALL(membership_updated_, ready());

  // Connection to the host is made to the downstream connection's local address.
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("FD00::1"));

  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb_->chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.addressProvider().localAddress(), *host->address());

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  // Check that 'second' also gets updated
  EXPECT_EQ(1UL, second.hostSetsPerPriority()[0]->hosts().size());

  EXPECT_EQ(host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(host, second.hostSetsPerPriority()[0]->hosts()[0]);
}

TEST_F(ClusterTest, UseHttpHeaderEnabled) {
  std::string yaml = default_yaml_ + R"EOF(
    original_dst_lb_config:
      use_http_header: true
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(yaml, false);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  Event::PostCb post_cb;

  // HTTP header override.
  TestLoadBalancerContext lb_context1(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host1 = lb_->chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ("127.0.0.1:5555", host1->address()->asString());

  // HTTP header override on downstream connection which isn't using original_dst filter
  // and/or is done over Unix Domain Socket. This works, because properties of the downstream
  // connection are never checked when using HTTP header override.
  NiceMock<Network::MockConnection> connection2;
  TestLoadBalancerContext lb_context2(&connection2, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5556");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host2 = lb_->chooseHost(&lb_context2);
  post_cb();
  ASSERT_NE(host2, nullptr);
  EXPECT_EQ("127.0.0.1:5556", host2->address()->asString());

  // HTTP header override with empty header value.
  TestLoadBalancerContext lb_context3(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(), "");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host3 = lb_->chooseHost(&lb_context3);
  EXPECT_EQ(host3, nullptr);
  EXPECT_EQ(
      1, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());

  // HTTP header override with invalid header value.
  TestLoadBalancerContext lb_context4(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "a.b.c.d");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host4 = lb_->chooseHost(&lb_context4);
  EXPECT_EQ(host4, nullptr);
  EXPECT_EQ(
      2, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());
}

TEST_F(ClusterTest, UseHttpHeaderDisabled) {
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(default_yaml_, false);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  Event::PostCb post_cb;

  // Downstream connection with original_dst filter, HTTP header override ignored.
  NiceMock<Network::MockConnection> connection1;
  connection1.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));
  TestLoadBalancerContext lb_context1(&connection1, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host1 = lb_->chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ(*connection1.addressProvider().localAddress(), *host1->address());

  // Downstream connection without original_dst filter, HTTP header override ignored.
  NiceMock<Network::MockConnection> connection2;
  connection2.stream_info_.downstream_address_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));
  TestLoadBalancerContext lb_context2(&connection2, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host2 = lb_->chooseHost(&lb_context2);
  EXPECT_EQ(host2, nullptr);

  // Downstream connection over Unix Domain Socket, HTTP header override ignored.
  NiceMock<Network::MockConnection> connection3;
  connection3.stream_info_.downstream_address_provider_->setLocalAddress(
      std::make_shared<Network::Address::PipeInstance>("unix://foo"));
  TestLoadBalancerContext lb_context3(&connection3, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host3 = lb_->chooseHost(&lb_context3);
  EXPECT_EQ(host3, nullptr);
}

TEST_F(ClusterTest, SNIFromHostHeader) {
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(default_tls_yaml_, true);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // SNI from Host header?
  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 80));
  TestLoadBalancerContext lb_context(&connection, Http::Headers::get().Host.get(),
                                     "www.google.com:80");
  // Verify TestLoadBalancerContext works
  auto downstream_headers = lb_context.downstreamHeaders();
  ASSERT_NE(downstream_headers, nullptr);
  auto host_header = downstream_headers->get(Http::Headers::get().Host);
  ASSERT_NE(host_header.empty(), true);
  ASSERT_EQ(host_header.size(), 1);
  auto host_header_string = std::string(host_header[0]->value().getStringView());
  ASSERT_EQ("www.google.com:80", host_header_string);

  Event::PostCb post_cb;
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb_->chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("10.10.11.11:80", host->address()->asString());
  const Cluster::Host* options_host = dynamic_cast<const Cluster::Host*>(host.get());
  ASSERT_NE(options_host, nullptr);
  const Network::TransportSocketOptionsSharedPtr options =
      getDefaultTransportSocketOptions(options_host);
  ASSERT_NE(options, nullptr);
  auto sni_override = options->serverNameOverride();
  ASSERT_EQ(true, !!sni_override);
  ASSERT_EQ("www.google.com", sni_override.value());
}

TEST_F(ClusterTest, NoSNIFromHostHeaderIP) {
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(default_tls_yaml_, true);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // SNI from Host header?
  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 80));
  TestLoadBalancerContext lb_context(&connection, Http::Headers::get().Host.get(), "[ffd0::1]:80");

  Event::PostCb post_cb;
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb_->chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("10.10.11.11:80", host->address()->asString());
  const Cluster::Host* options_host = dynamic_cast<const Cluster::Host*>(host.get());
  ASSERT_NE(options_host, nullptr);
  const Network::TransportSocketOptionsSharedPtr options =
      getDefaultTransportSocketOptions(options_host);
  ASSERT_EQ(options, nullptr);
}

TEST_F(ClusterTest, NoTLSNoSNIFromHostHeader) {
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(default_yaml_, false);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // SNI from Host header?
  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 80));
  TestLoadBalancerContext lb_context(&connection, Http::Headers::get().Host.get(),
                                     "www.google.com:80");

  Event::PostCb post_cb;
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb_->chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("10.10.11.11:80", host->address()->asString());
  const Cluster::Host* options_host = dynamic_cast<const Cluster::Host*>(host.get());
  ASSERT_NE(options_host, nullptr);
  const Network::TransportSocketOptionsSharedPtr options =
      getDefaultTransportSocketOptions(options_host);
  ASSERT_EQ(options, nullptr);
}

TEST_F(ClusterTest, SNIFromDownstreamConnection) {
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  setupFromYaml(default_tls_yaml_, true);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // SNI from downstream connection, even if Host header is present
  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.downstream_address_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 80));
  EXPECT_CALL(connection, requestedServerName()).WillRepeatedly(Return("www.isovalent.com"));
  TestLoadBalancerContext lb_context(&connection, Http::Headers::get().Host.get(),
                                     "www.google.com:80");

  Event::PostCb post_cb;
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb_->chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("10.10.11.11:80", host->address()->asString());
  const Cluster::Host* options_host = dynamic_cast<const Cluster::Host*>(host.get());
  ASSERT_NE(options_host, nullptr);
  const Network::TransportSocketOptionsSharedPtr options =
      getDefaultTransportSocketOptions(options_host);
  ASSERT_NE(options, nullptr);
  auto sni_override = options->serverNameOverride();
  ASSERT_EQ(true, !!sni_override);
  ASSERT_EQ("www.isovalent.com", sni_override.value());
}

} // namespace OriginalDst
} // namespace Upstream
} // namespace Envoy
