#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/stats/scope.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/upstream/original_dst_cluster.h"
#include "common/upstream/upstream_impl.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Upstream {
namespace OriginalDstClusterTest {

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  TestLoadBalancerContext(const Network::Connection* connection) : connection_(connection) {}
  TestLoadBalancerContext(const Network::Connection* connection, const std::string& key,
                          const std::string& value)
      : connection_(connection) {
    downstream_headers_ = Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{key, value}}};
  }

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return 0; }
  const Network::Connection* downstreamConnection() const override { return connection_; }
  const Http::HeaderMap* downstreamHeaders() const override { return downstream_headers_.get(); }

  absl::optional<uint64_t> hash_key_;
  const Network::Connection* connection_;
  Http::HeaderMapPtr downstream_headers_;
};

class OriginalDstClusterTest : public testing::Test {
public:
  // cleanup timer must be created before the cluster (in setup()), so that we can set expectations
  // on it. Ownership is transferred to the cluster at the cluster constructor, so the cluster will
  // take care of destructing it!
  OriginalDstClusterTest() : cleanup_timer_(new Event::MockTimer(&dispatcher_)) {}

  void setupFromJson(const std::string& json) { setup(parseClusterFromJson(json)); }
  void setupFromYaml(const std::string& yaml) { setup(parseClusterFromV2Yaml(yaml)); }

  void setup(const envoy::api::v2::Cluster& cluster_config) {
    NiceMock<MockClusterManager> cm;
    Envoy::Stats::ScopePtr scope = stats_store_.createScope(fmt::format(
        "cluster.{}.", cluster_config.alt_stat_name().empty() ? cluster_config.name()
                                                              : cluster_config.alt_stat_name()));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        ssl_context_manager_, *scope, cm, local_info_, dispatcher_, random_, stats_store_);
    cluster_.reset(
        new OriginalDstCluster(cluster_config, runtime_, factory_context, std::move(scope), false));
    cluster_->prioritySet().addMemberUpdateCb(
        [&](uint32_t, const HostVector&, const HostVector&) -> void {
          membership_updated_.ready();
        });
    cluster_->initialize([&]() -> void { initialized_.ready(); });
  }

  Stats::IsolatedStoreImpl stats_store_;
  Ssl::MockContextManager ssl_context_manager_;
  ClusterSharedPtr cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Event::MockTimer* cleanup_timer_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

TEST(OriginalDstClusterConfigTest, BadConfig) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "original_dst",
    "lb_type": "original_dst_lb",
    "hosts": [{"url": "tcp://foo.bar.com:443"}]
  }
  )EOF"; // Help Emacs balance quotation marks: "

  EXPECT_THROW(parseClusterFromJson(json), EnvoyException);
}

TEST(OriginalDstClusterConfigTest, GoodConfig) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "original_dst",
    "lb_type": "original_dst_lb",
    "cleanup_interval_ms": 1000
  }
  )EOF"; // Help Emacs balance quotation marks: "

  EXPECT_TRUE(parseClusterFromJson(json).has_cleanup_interval());
}

TEST_F(OriginalDstClusterTest, CleanupInterval) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 250,
    "type": "original_dst",
    "lb_type": "original_dst_lb",
    "cleanup_interval_ms": 1000
  }
  )EOF"; // Help Emacs balance quotation marks: "

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(*cleanup_timer_, enableTimer(std::chrono::milliseconds(1000)));
  setupFromJson(json);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

TEST_F(OriginalDstClusterTest, NoContext) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 1250,
    "type": "original_dst",
    "lb_type": "original_dst_lb"
  }
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  setupFromJson(json);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // No downstream connection => no host.
  {
    TestLoadBalancerContext lb_context(nullptr);
    OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_,
                                        cluster_->info()->lbOriginalDstConfig());
    EXPECT_CALL(dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb.chooseHost(&lb_context);
    EXPECT_EQ(host, nullptr);
  }

  // Downstream connection is not using original dst => no host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);

    EXPECT_CALL(connection, localAddressRestored()).WillOnce(Return(false));
    // First argument is normally the reference to the ThreadLocalCluster's HostSet, but in these
    // tests we do not have the thread local clusters, so we pass a reference to the HostSet of the
    // primary cluster. The implementation handles both cases the same.
    OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_,
                                        cluster_->info()->lbOriginalDstConfig());
    EXPECT_CALL(dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb.chooseHost(&lb_context);
    EXPECT_EQ(host, nullptr);
  }

  // No host for non-IP address
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    connection.local_address_ = std::make_shared<Network::Address::PipeInstance>("unix://foo");
    EXPECT_CALL(connection, localAddressRestored()).WillRepeatedly(Return(true));

    OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_,
                                        cluster_->info()->lbOriginalDstConfig());
    EXPECT_CALL(dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb.chooseHost(&lb_context);
    EXPECT_EQ(host, nullptr);
  }
}

TEST_F(OriginalDstClusterTest, Membership) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 1250,
    "type": "original_dst",
    "lb_type": "original_dst_lb"
  }
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  setupFromJson(json);

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
  connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11");
  EXPECT_CALL(connection, localAddressRestored()).WillRepeatedly(Return(true));

  OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_,
                                      cluster_->info()->lbOriginalDstConfig());
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb.chooseHost(&lb_context);
  post_cb();
  auto cluster_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();

  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.local_address_, *host->address());

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  EXPECT_EQ(host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(*connection.local_address_,
            *cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address());

  // Same host is returned on the 2nd call
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context);
  EXPECT_EQ(host2, host);

  // Make host time out, no membership changes happen on the first timeout.
  ASSERT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(true, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->used());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  cleanup_timer_->callback_();
  EXPECT_EQ(
      cluster_hosts,
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector remains the same

  // host gets removed on the 2nd timeout.
  ASSERT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(false, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->used());

  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->callback_();
  EXPECT_NE(cluster_hosts,
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector changes

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  cluster_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();

  // New host gets created
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host3 = lb.chooseHost(&lb_context);
  post_cb();
  EXPECT_NE(host3, nullptr);
  EXPECT_NE(host3, host);
  EXPECT_NE(cluster_hosts,
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector changes

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(host3, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
}

TEST_F(OriginalDstClusterTest, Membership2) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 1250,
    "type": "original_dst",
    "lb_type": "original_dst_lb"
  }
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  setupFromJson(json);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // Host gets the local address of the downstream connection.

  NiceMock<Network::MockConnection> connection1;
  TestLoadBalancerContext lb_context1(&connection1);
  connection1.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11");
  EXPECT_CALL(connection1, localAddressRestored()).WillRepeatedly(Return(true));

  NiceMock<Network::MockConnection> connection2;
  TestLoadBalancerContext lb_context2(&connection2);
  connection2.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.11.12");
  EXPECT_CALL(connection2, localAddressRestored()).WillRepeatedly(Return(true));

  OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_,
                                      cluster_->info()->lbOriginalDstConfig());

  EXPECT_CALL(membership_updated_, ready());
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ(*connection1.local_address_, *host1->address());

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context2);
  post_cb();
  ASSERT_NE(host2, nullptr);
  EXPECT_EQ(*connection2.local_address_, *host2->address());

  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  EXPECT_EQ(host1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(*connection1.local_address_,
            *cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address());

  EXPECT_EQ(host2, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]);
  EXPECT_EQ(*connection2.local_address_,
            *cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]->address());

  auto cluster_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();

  // Make hosts time out, no membership changes happen on the first timeout.
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(true, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->used());
  EXPECT_EQ(true, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]->used());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  cleanup_timer_->callback_();
  EXPECT_EQ(
      cluster_hosts,
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector remains the same

  // both hosts get removed on the 2nd timeout.
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(false, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->used());
  EXPECT_EQ(false, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]->used());

  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->callback_();
  EXPECT_NE(cluster_hosts,
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector changes

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(OriginalDstClusterTest, Connection) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 1250,
    "type": "original_dst",
    "lb_type": "original_dst_lb"
  }
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  setupFromJson(json);

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
  connection.local_address_ = std::make_shared<Network::Address::Ipv6Instance>("FD00::1");
  EXPECT_CALL(connection, localAddressRestored()).WillRepeatedly(Return(true));

  OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_,
                                      cluster_->info()->lbOriginalDstConfig());
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb.chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.local_address_, *host->address());

  EXPECT_CALL(dispatcher_, createClientConnection_(PointeesEq(connection.local_address_), _, _, _))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  host->createConnection(dispatcher_, nullptr, nullptr);
}

TEST_F(OriginalDstClusterTest, MultipleClusters) {
  std::string json = R"EOF(
  {
    "name": "name",
    "connect_timeout_ms": 1250,
    "type": "original_dst",
    "lb_type": "original_dst_lb"
  }
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  setupFromJson(json);

  PrioritySetImpl second;
  cluster_->prioritySet().addMemberUpdateCb(
      [&](uint32_t, const HostVector& added, const HostVector& removed) -> void {
        // Update second hostset accordingly;
        HostVectorSharedPtr new_hosts(
            new HostVector(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()));
        HostVectorSharedPtr healthy_hosts(
            new HostVector(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()));
        const HostsPerLocalityConstSharedPtr empty_hosts_per_locality{new HostsPerLocalityImpl()};

        second.getOrCreateHostSet(0).updateHosts(new_hosts, healthy_hosts, empty_hosts_per_locality,
                                                 empty_hosts_per_locality, {}, added, removed,
                                                 absl::nullopt);
      });

  EXPECT_CALL(membership_updated_, ready());

  // Connection to the host is made to the downstream connection's local address.
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.local_address_ = std::make_shared<Network::Address::Ipv6Instance>("FD00::1");
  EXPECT_CALL(connection, localAddressRestored()).WillRepeatedly(Return(true));

  OriginalDstCluster::LoadBalancer lb1(cluster_->prioritySet(), cluster_,
                                       cluster_->info()->lbOriginalDstConfig());
  OriginalDstCluster::LoadBalancer lb2(second, cluster_, cluster_->info()->lbOriginalDstConfig());
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb1.chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.local_address_, *host->address());

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  // Check that lb2 also gets updated
  EXPECT_EQ(1UL, second.hostSetsPerPriority()[0]->hosts().size());

  EXPECT_EQ(host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(host, second.hostSetsPerPriority()[0]->hosts()[0]);
}

TEST_F(OriginalDstClusterTest, UseHttpHeaderEnabled) {
  std::string yaml = R"EOF(
    name: "name"
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: ORIGINAL_DST_LB
    original_dst_lb_config:
      use_http_header: true
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_,
                                      cluster_->info()->lbOriginalDstConfig());
  Event::PostCb post_cb;

  // HTTP header override.
  TestLoadBalancerContext lb_context1(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ("127.0.0.1:5555", host1->address()->asString());

  // HTTP header override on downstream connection which isn't using original_dst filter
  // and/or is done over Unix Domain Socket. This works, because properties of the downstream
  // connection are never checked when using HTTP header override.
  NiceMock<Network::MockConnection> connection2;
  EXPECT_CALL(connection2, localAddress()).Times(0);
  EXPECT_CALL(connection2, localAddressRestored()).Times(0);
  TestLoadBalancerContext lb_context2(&connection2, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5556");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context2);
  post_cb();
  ASSERT_NE(host2, nullptr);
  EXPECT_EQ("127.0.0.1:5556", host2->address()->asString());

  // HTTP header override with empty header value.
  TestLoadBalancerContext lb_context3(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(), "");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host3 = lb.chooseHost(&lb_context3);
  EXPECT_EQ(host3, nullptr);
  EXPECT_EQ(
      1, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());

  // HTTP header override with invalid header value.
  TestLoadBalancerContext lb_context4(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "a.b.c.d");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host4 = lb.chooseHost(&lb_context4);
  EXPECT_EQ(host4, nullptr);
  EXPECT_EQ(
      2, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());
}

TEST_F(OriginalDstClusterTest, UseHttpHeaderDisabled) {
  std::string yaml = R"EOF(
    name: "name"
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: ORIGINAL_DST_LB
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_));
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_,
                                      cluster_->info()->lbOriginalDstConfig());
  Event::PostCb post_cb;

  // Downstream connection with original_dst filter, HTTP header override ignored.
  NiceMock<Network::MockConnection> connection1;
  connection1.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11");
  EXPECT_CALL(connection1, localAddressRestored()).WillOnce(Return(true));
  TestLoadBalancerContext lb_context1(&connection1, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ(*connection1.local_address_, *host1->address());

  // Downstream connection without original_dst filter, HTTP header override ignored.
  NiceMock<Network::MockConnection> connection2;
  connection2.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11");
  EXPECT_CALL(connection2, localAddressRestored()).WillOnce(Return(false));
  TestLoadBalancerContext lb_context2(&connection2, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context2);
  EXPECT_EQ(host2, nullptr);

  // Downstream connection over Unix Domain Socket, HTTP header override ignored.
  NiceMock<Network::MockConnection> connection3;
  connection3.local_address_ = std::make_shared<Network::Address::PipeInstance>("unix://foo");
  TestLoadBalancerContext lb_context3(&connection3, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host3 = lb.chooseHost(&lb_context3);
  EXPECT_EQ(host3, nullptr);
}

} // namespace OriginalDstClusterTest
} // namespace Upstream
} // namespace Envoy
