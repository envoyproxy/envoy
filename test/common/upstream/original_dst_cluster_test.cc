#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/upstream/original_dst_cluster.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Upstream {
namespace OriginalDstClusterTest {

class TestLoadBalancerContext : public LoadBalancerContext {
public:
  TestLoadBalancerContext(const Network::Connection* connection) : connection_(connection) {}

  // Upstream::LoadBalancerContext
  Optional<uint64_t> computeHashKey() override { return 0; }
  const Network::Connection* downstreamConnection() const override { return connection_; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override { return nullptr; }

  Optional<uint64_t> hash_key_;
  const Network::Connection* connection_;
};

class OriginalDstClusterTest : public testing::Test {
public:
  // cleanup timer must be created before the cluster (in setup()), so that we can set expectations
  // on it. Ownership is transferred to the cluster at the cluster constructor, so the cluster will
  // take care of destructing it!
  OriginalDstClusterTest() : cleanup_timer_(new Event::MockTimer(&dispatcher_)) {}

  void setup(const std::string& json) {
    NiceMock<MockClusterManager> cm;
    cluster_.reset(new OriginalDstCluster(parseClusterFromJson(json), runtime_, stats_store_,
                                          ssl_context_manager_, cm, dispatcher_, false));
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
  setup(json);

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
  setup(json);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // No downstream connection => no host.
  {
    TestLoadBalancerContext lb_context(nullptr);
    OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_);
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
    OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_);
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

    OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_);
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
  setup(json);

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

  OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_);
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
  setup(json);

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

  OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_);

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
  setup(json);

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

  OriginalDstCluster::LoadBalancer lb(cluster_->prioritySet(), cluster_);
  Event::PostCb post_cb;
  EXPECT_CALL(dispatcher_, post(_)).WillOnce(SaveArg<0>(&post_cb));
  HostConstSharedPtr host = lb.chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.local_address_, *host->address());

  EXPECT_CALL(dispatcher_, createClientConnection_(PointeesEq(connection.local_address_), _, _, _))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  host->createConnection(dispatcher_, nullptr);
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
  setup(json);

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
                                                 empty_hosts_per_locality, added, removed);
      });

  EXPECT_CALL(membership_updated_, ready());

  // Connection to the host is made to the downstream connection's local address.
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.local_address_ = std::make_shared<Network::Address::Ipv6Instance>("FD00::1");
  EXPECT_CALL(connection, localAddressRestored()).WillRepeatedly(Return(true));

  OriginalDstCluster::LoadBalancer lb1(cluster_->prioritySet(), cluster_);
  OriginalDstCluster::LoadBalancer lb2(second, cluster_);
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

} // namespace OriginalDstClusterTest
} // namespace Upstream
} // namespace Envoy
