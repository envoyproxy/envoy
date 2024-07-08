#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/filter_state_dst_address.h"
#include "source/common/network/utility.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/original_dst/original_dst_cluster.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  TestLoadBalancerContext(const Network::Connection* connection)
      : TestLoadBalancerContext(connection, nullptr) {}
  TestLoadBalancerContext(const Network::Connection* connection,
                          const StreamInfo::StreamInfo* request_stream_info)
      : connection_(connection), request_stream_info_(request_stream_info) {}
  TestLoadBalancerContext(const Network::Connection* connection, const std::string& key,
                          const std::string& value)
      : TestLoadBalancerContext(connection) {
    downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{key, value}}};
  }

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return 0; }
  const Network::Connection* downstreamConnection() const override { return connection_; }
  const StreamInfo::StreamInfo* requestStreamInfo() const override { return request_stream_info_; }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return downstream_headers_.get();
  }

  absl::optional<uint64_t> hash_key_;
  const Network::Connection* connection_;
  const StreamInfo::StreamInfo* request_stream_info_;
  Http::RequestHeaderMapPtr downstream_headers_;
};

class OriginalDstClusterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  OriginalDstClusterTest() = default;

  void setupFromYaml(const std::string& yaml, bool expect_success = true) {
    if (expect_success) {
      cleanup_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
      EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
    }
    setup(parseClusterFromV3Yaml(yaml));
  }

  void setup(const envoy::config::cluster::v3::Cluster& cluster_config) {
    Envoy::Upstream::ClusterFactoryContextImpl factory_context(
        server_context_, server_context_.cluster_manager_, nullptr, ssl_context_manager_, nullptr,
        false);

    OriginalDstClusterFactory factory;
    auto status_or_pair = factory.createClusterImpl(cluster_config, factory_context);
    THROW_IF_STATUS_NOT_OK(status_or_pair, throw);

    cluster_ = std::dynamic_pointer_cast<OriginalDstCluster>(status_or_pair.value().first);
    priority_update_cb_ = cluster_->prioritySet().addPriorityUpdateCb(
        [&](uint32_t, const HostVector&, const HostVector&) {
          membership_updated_.ready();
          return absl::OkStatus();
        });
    ON_CALL(initialized_, ready()).WillByDefault(testing::Invoke([this] {
      init_complete_ = true;
    }));
    cluster_->initialize([&]() -> void { initialized_.ready(); });
    handle_ = std::make_shared<OriginalDstClusterHandle>(cluster_);
  }

  void TearDown() override {
    if (init_complete_) {
      EXPECT_CALL(server_context_.dispatcher_, post(_));
      EXPECT_CALL(*cleanup_timer_, disableTimer());
    }
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  Stats::TestUtil::TestStore& stats_store_ = server_context_.store_;
  Ssl::MockContextManager ssl_context_manager_;

  std::shared_ptr<OriginalDstCluster> cluster_;
  OriginalDstClusterHandleSharedPtr handle_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  Event::MockTimer* cleanup_timer_;
  Common::CallbackHandlePtr priority_update_cb_;
  bool init_complete_{false};
};

namespace {
TEST(OriginalDstClusterConfigTest, GoodConfig) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: original_dst
    lb_policy: cluster_provided
    cleanup_interval: 1s
  )EOF"; // Help Emacs balance quotation marks: "

  EXPECT_TRUE(parseClusterFromV3Yaml(yaml).has_cleanup_interval());
}

TEST_F(OriginalDstClusterTest, BadConfigWithLoadAssignment) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
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

  EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                            "ORIGINAL_DST clusters must have no load assignment configured");
}

TEST_F(OriginalDstClusterTest, CleanupInterval) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
  )EOF"; // Help Emacs balance quotation marks: "

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(membership_updated_, ready()).Times(0);
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

TEST_F(OriginalDstClusterTest, NoContext) {
  std::string yaml = R"EOF(
    name: name,
    connect_timeout: 0.125s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(membership_updated_, ready()).Times(0);
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // No downstream connection => no host.
  {
    TestLoadBalancerContext lb_context(nullptr);
    OriginalDstCluster::LoadBalancer lb(handle_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb.chooseHost(&lb_context);
    EXPECT_EQ(host, nullptr);

    EXPECT_EQ(nullptr, lb.peekAnotherHost(nullptr));
    EXPECT_FALSE(lb.lifetimeCallbacks().has_value());
    std::vector<uint8_t> hash_key;
    auto mock_host = std::make_shared<NiceMock<MockHost>>();
    EXPECT_FALSE(lb.selectExistingConnection(nullptr, *mock_host, hash_key).has_value());
  }

  // Downstream connection is not using original dst => no host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);

    // First argument is normally the reference to the ThreadLocalCluster's HostSet, but in these
    // tests we do not have the thread local clusters, so we pass a reference to the HostSet of the
    // primary cluster. The implementation handles both cases the same.
    OriginalDstCluster::LoadBalancer lb(handle_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb.chooseHost(&lb_context);
    EXPECT_EQ(host, nullptr);
  }

  // No host for non-IP address
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
        *Network::Address::PipeInstance::create("unix://foo"));

    OriginalDstCluster::LoadBalancer lb(handle_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    HostConstSharedPtr host = lb.chooseHost(&lb_context);
    EXPECT_EQ(host, nullptr);
  }
}

TEST_F(OriginalDstClusterTest, AddressCollision) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Host gets the local address of the downstream connection.
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  // Mock the cluster manager by recreating the load balancer each time to get a fresh host map
  auto lb1 = OriginalDstCluster::LoadBalancer(handle_);
  auto lb2 = OriginalDstCluster::LoadBalancer(handle_);

  // Simulate concurrent request for the same address from two workers.
  Event::PostCb post_cb1;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb1](Event::PostCb cb) {
    post_cb1 = std::move(cb);
  });
  HostConstSharedPtr host1 = lb1.chooseHost(&lb_context);
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ(*connection.connectionInfoProvider().localAddress(), *host1->address());
  Event::PostCb post_cb2;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb2](Event::PostCb cb) {
    post_cb2 = std::move(cb);
  });
  HostConstSharedPtr host2 = lb2.chooseHost(&lb_context);
  ASSERT_NE(host2, nullptr);
  EXPECT_EQ(*connection.connectionInfoProvider().localAddress(), *host2->address());

  // Process main callbacks sequentially.
  EXPECT_CALL(membership_updated_, ready());
  post_cb1();
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_CALL(membership_updated_, ready());
  post_cb2();
  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());

  // First host is returned on the 3rd call
  HostConstSharedPtr host3 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);
  EXPECT_EQ(host3, host1);

  // Make host time out, no membership changes happen on the first timeout.
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  cleanup_timer_->invokeCallback();

  // Both hosts get removed on the 2nd timeout.
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());

  // New host gets created once again.
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb1](Event::PostCb cb) {
    post_cb1 = std::move(cb);
  });
  // Mock the cluster manager by recreating the load balancer with the new host map
  HostConstSharedPtr host4 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);
  post_cb1();
  EXPECT_NE(host4, nullptr);
  EXPECT_NE(host4, host1);
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(OriginalDstClusterTest, HostInUse) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Host gets the local address of the downstream connection.
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  // Mock the cluster manager by recreating the load balancer each time to get a fresh host map
  auto lb = OriginalDstCluster::LoadBalancer(handle_);
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);
  EXPECT_CALL(membership_updated_, ready());
  post_cb();

  // Borrow a host handle.
  auto handle = host->acquireHandle();

  // Run cleanup 3 times, and validate the host is not removed.
  for (auto i = 0; i < 3; i++) {
    EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
    cleanup_timer_->invokeCallback();
  }
  HostConstSharedPtr host2 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);
  EXPECT_EQ(host2, host);

  // Reset a handle, and run cleanup twice again.
  handle.reset();
  ASSERT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  cleanup_timer_->invokeCallback();
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(OriginalDstClusterTest, CollisionHostInUse) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Host gets the local address of the downstream connection.
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  // Mock the cluster manager by recreating the load balancer each time to get a fresh host map
  auto lb = OriginalDstCluster::LoadBalancer(handle_);
  Event::PostCb post_cb1;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb1](Event::PostCb cb) {
    post_cb1 = std::move(cb);
  });
  HostConstSharedPtr host1 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);
  Event::PostCb post_cb2;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb2](Event::PostCb cb) {
    post_cb2 = std::move(cb);
  });
  HostConstSharedPtr host2 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);

  EXPECT_CALL(membership_updated_, ready());
  post_cb1();
  EXPECT_CALL(membership_updated_, ready());
  post_cb2();

  // Borrow a collision host2 handle.
  auto handle = host2->acquireHandle();

  // Run cleanup 3 times, and validate the host is not removed.
  for (auto i = 0; i < 3; i++) {
    EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
    cleanup_timer_->invokeCallback();
  }
  HostConstSharedPtr host3 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);
  EXPECT_EQ(host3, host1);

  // Reset a handle, and run cleanup twice again.
  handle.reset();
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  cleanup_timer_->invokeCallback();
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(OriginalDstClusterTest, Membership) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

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
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  // Mock the cluster manager by recreating the load balancer each time to get a fresh host map
  HostConstSharedPtr host = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);
  post_cb();
  auto cluster_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();

  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.connectionInfoProvider().localAddress(), *host->address());

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  EXPECT_EQ(host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(*connection.connectionInfoProvider().localAddress(),
            *cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address());

  // Same host is returned on the 2nd call
  // Mock the cluster manager by recreating the load balancer with the new host map
  HostConstSharedPtr host2 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);
  EXPECT_EQ(host2, host);

  // Make host time out, no membership changes happen on the first timeout.
  ASSERT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(
      cluster_hosts,
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector remains the same

  // host gets removed on the 2nd timeout.
  ASSERT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());

  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->invokeCallback();
  EXPECT_NE(cluster_hosts,
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector changes

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  cluster_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();

  // New host gets created
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  // Mock the cluster manager by recreating the load balancer with the new host map
  HostConstSharedPtr host3 = OriginalDstCluster::LoadBalancer(handle_).chooseHost(&lb_context);
  post_cb();
  EXPECT_NE(host3, nullptr);
  EXPECT_NE(host3, host);
  EXPECT_NE(cluster_hosts,
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector changes

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(host3, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
}

TEST_F(OriginalDstClusterTest, Membership2) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  // Host gets the local address of the downstream connection.

  NiceMock<Network::MockConnection> connection1;
  TestLoadBalancerContext lb_context1(&connection1);
  connection1.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));

  NiceMock<Network::MockConnection> connection2;
  TestLoadBalancerContext lb_context2(&connection2);
  connection2.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.12"));

  OriginalDstCluster::LoadBalancer lb(handle_);
  EXPECT_CALL(membership_updated_, ready());
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ(*connection1.connectionInfoProvider().localAddress(), *host1->address());

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context2);
  post_cb();
  ASSERT_NE(host2, nullptr);
  EXPECT_EQ(*connection2.connectionInfoProvider().localAddress(), *host2->address());

  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  EXPECT_EQ(host1, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(*connection1.connectionInfoProvider().localAddress(),
            *cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->address());

  EXPECT_EQ(host2, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]);
  EXPECT_EQ(*connection2.connectionInfoProvider().localAddress(),
            *cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[1]->address());

  auto cluster_hosts = cluster_->prioritySet().hostSetsPerPriority()[0]->hosts();

  // Make hosts time out, no membership changes happen on the first timeout.
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  cleanup_timer_->invokeCallback();
  EXPECT_EQ(
      cluster_hosts,
      cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector remains the same

  // both hosts get removed on the 2nd timeout.
  ASSERT_EQ(2UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());

  EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
  EXPECT_CALL(membership_updated_, ready());
  cleanup_timer_->invokeCallback();
  EXPECT_NE(cluster_hosts,
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()); // hosts vector changes

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
}

TEST_F(OriginalDstClusterTest, Connection) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

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
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("FD00::1"));

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.connectionInfoProvider().localAddress(), *host->address());

  EXPECT_CALL(server_context_.dispatcher_,
              createClientConnection_(
                  PointeesEq(connection.connectionInfoProvider().localAddress()), _, _, _))
      .WillOnce(Return(new NiceMock<Network::MockClientConnection>()));
  host->createConnection(server_context_.dispatcher_, nullptr, nullptr);
}

TEST_F(OriginalDstClusterTest, MultipleClusters) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  PrioritySetImpl second;
  auto priority_update_cb = cluster_->prioritySet().addPriorityUpdateCb(
      [&](uint32_t, const HostVector& added, const HostVector& removed) {
        // Update second hostset accordingly;
        HostVectorSharedPtr new_hosts(
            new HostVector(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()));
        auto healthy_hosts = std::make_shared<const HealthyHostVector>(
            cluster_->prioritySet().hostSetsPerPriority()[0]->hosts());
        const HostsPerLocalityConstSharedPtr empty_hosts_per_locality{new HostsPerLocalityImpl()};

        second.updateHosts(0,
                           updateHostsParams(new_hosts, empty_hosts_per_locality, healthy_hosts,
                                             empty_hosts_per_locality),
                           {}, added, removed, 0, absl::nullopt);
        return absl::OkStatus();
      });

  EXPECT_CALL(membership_updated_, ready());

  // Connection to the host is made to the downstream connection's local address.
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("FD00::1"));

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ(*connection.connectionInfoProvider().localAddress(), *host->address());

  EXPECT_EQ(1UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  // Check that 'second' also gets updated
  EXPECT_EQ(1UL, second.hostSetsPerPriority()[0]->hosts().size());

  EXPECT_EQ(host, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]);
  EXPECT_EQ(host, second.hostSetsPerPriority()[0]->hosts()[0]);
}

TEST_F(OriginalDstClusterTest, UseHttpHeaderEnabled) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      use_http_header: true
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  // HTTP header override.
  TestLoadBalancerContext lb_context1(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
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
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context2);
  post_cb();
  ASSERT_NE(host2, nullptr);
  EXPECT_EQ("127.0.0.1:5556", host2->address()->asString());

  // HTTP header override with empty header value.
  TestLoadBalancerContext lb_context3(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(), "");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host3 = lb.chooseHost(&lb_context3);
  EXPECT_EQ(host3, nullptr);
  EXPECT_EQ(
      1, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());

  // HTTP header override with invalid header value.
  TestLoadBalancerContext lb_context4(nullptr, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "a.b.c.d");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host4 = lb.chooseHost(&lb_context4);
  EXPECT_EQ(host4, nullptr);
  EXPECT_EQ(
      2, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());
}

// Verify original dst cluster can read from HTTP host header. The corner cases are tested in
// `UseHttpHeaderEnabled`.
TEST_F(OriginalDstClusterTest, UseHttpAuthorityHeader) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      use_http_header: true
      http_header_name: ":authority"
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  // HTTP header override by `:authority`.
  TestLoadBalancerContext lb_context1(nullptr, Http::Headers::get().Host.get(), "127.0.0.1:6666");
  lb_context1.downstream_headers_->setCopy(Http::Headers::get().EnvoyOriginalDstHost,
                                           "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ("127.0.0.1:6666", host1->address()->asString());
}

TEST_F(OriginalDstClusterTest, BadConfigWithHttpHeaderNameAndClearedUseHttpHeader) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      http_header_name: ":authority"
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      setupFromYaml(yaml, false), EnvoyException,
      "ORIGINAL_DST cluster: invalid config http_header_name=:authority and use_http_header is "
      "false. Set use_http_header to true if http_header_name is desired.");
}

TEST_F(OriginalDstClusterTest, UseHttpHeaderDisabled) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  // Downstream connection with original_dst filter, HTTP header override ignored.
  NiceMock<Network::MockConnection> connection1;
  connection1.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));
  TestLoadBalancerContext lb_context1(&connection1, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ(*connection1.connectionInfoProvider().localAddress(), *host1->address());

  // Downstream connection without original_dst filter, HTTP header override ignored.
  NiceMock<Network::MockConnection> connection2;
  connection2.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11"));
  TestLoadBalancerContext lb_context2(&connection2, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host2 = lb.chooseHost(&lb_context2);
  EXPECT_EQ(host2, nullptr);

  // Downstream connection over Unix Domain Socket, HTTP header override ignored.
  NiceMock<Network::MockConnection> connection3;
  connection3.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      *Network::Address::PipeInstance::create("unix://foo"));
  TestLoadBalancerContext lb_context3(&connection3, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host3 = lb.chooseHost(&lb_context3);
  EXPECT_EQ(host3, nullptr);
}

TEST_F(OriginalDstClusterTest, UseMetadataKeyWithRequest) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      metadata_key:
        key: xxx
        path:
        - key: a
        - key: b
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  NiceMock<StreamInfo::MockStreamInfo> request_stream_info;
  TestLoadBalancerContext lb_context(&connection, &request_stream_info);

  envoy::config::core::v3::Metadata req_dynamic_metadata;
  (*(*(*req_dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"]
        .mutable_struct_value()
        ->mutable_fields())["b"]
      .set_string_value("127.0.0.1:6666");
  envoy::config::core::v3::Metadata conn_dynamic_metadata;
  (*(*(*conn_dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"]
        .mutable_struct_value()
        ->mutable_fields())["b"]
      .set_string_value("127.0.0.1:7777");

  EXPECT_CALL(Const(request_stream_info), dynamicMetadata())
      .WillRepeatedly(ReturnRef(req_dynamic_metadata));
  EXPECT_CALL(Const(connection.stream_info_), dynamicMetadata())
      .WillRepeatedly(ReturnRef(conn_dynamic_metadata));

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("127.0.0.1:6666", host->address()->asString());
}

TEST_F(OriginalDstClusterTest, UseMetadataKeyNoRequest) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      metadata_key:
        key: xxx
        path:
        - key: a
        - key: b
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);

  envoy::config::core::v3::Metadata dynamic_metadata;
  (*(*(*dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"]
        .mutable_struct_value()
        ->mutable_fields())["b"]
      .set_string_value("127.0.0.1:6666");

  EXPECT_CALL(Const(connection.stream_info_), dynamicMetadata())
      .WillRepeatedly(ReturnRef(dynamic_metadata));

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("127.0.0.1:6666", host->address()->asString());
}

TEST_F(OriginalDstClusterTest, UseMetadataKeyInvalidValue) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      metadata_key:
        key: xxx
        path:
        - key: a
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);

  envoy::config::core::v3::Metadata dynamic_metadata;
  (*(*dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"].set_string_value(
      "$IP$");

  EXPECT_CALL(Const(connection.stream_info_), dynamicMetadata())
      .WillRepeatedly(ReturnRef(dynamic_metadata));

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
  HostConstSharedPtr host = lb.chooseHost(&lb_context);
  EXPECT_EQ(host, nullptr);
  EXPECT_EQ(
      1, TestUtility::findCounter(stats_store_, "cluster.name.original_dst_host_invalid")->value());
}

TEST_F(OriginalDstClusterTest, UseMetadataKeyListValue) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      metadata_key:
        key: xxx
        path:
        - key: a
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);

  envoy::config::core::v3::Metadata dynamic_metadata;
  (*(*dynamic_metadata.mutable_filter_metadata())["xxx"].mutable_fields())["a"]
      .mutable_list_value()
      ->add_values()
      ->set_string_value("127.0.0.1:6666");

  EXPECT_CALL(Const(connection.stream_info_), dynamicMetadata())
      .WillRepeatedly(ReturnRef(dynamic_metadata));

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host = lb.chooseHost(&lb_context);
  post_cb();
  ASSERT_NE(host, nullptr);
  EXPECT_EQ("127.0.0.1:6666", host->address()->asString());
}

TEST_F(OriginalDstClusterTest, UseFilterState) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      use_http_header: true
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  // Filter state takes priority over header override.
  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.filterState()->setData(
      Upstream::OriginalDstClusterFilterStateKey,
      std::make_shared<Network::AddressObject>(
          std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 6666)),
      StreamInfo::FilterState::StateType::ReadOnly);
  TestLoadBalancerContext lb_context1(&connection, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ("10.10.11.11:6666", host1->address()->asString());
}

TEST_F(OriginalDstClusterTest, UsePortOverride) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      upstream_port_override: 443
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.downstream_connection_info_provider_->restoreLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 80));

  TestLoadBalancerContext lb_context1(&connection);
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  EXPECT_EQ("10.10.11.11:443", host1->address()->asString());
}

TEST_F(OriginalDstClusterTest, UseFilterStateWithPortOverride) {
  std::string yaml = R"EOF(
    name: name
    connect_timeout: 1.250s
    type: ORIGINAL_DST
    lb_policy: CLUSTER_PROVIDED
    original_dst_lb_config:
      use_http_header: true
      upstream_port_override: 443
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  OriginalDstCluster::LoadBalancer lb(handle_);
  Event::PostCb post_cb;

  // Filter state takes priority over header override.
  NiceMock<Network::MockConnection> connection;
  connection.stream_info_.filterState()->setData(
      Upstream::OriginalDstClusterFilterStateKey,
      std::make_shared<Network::AddressObject>(
          std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11", 6666)),
      StreamInfo::FilterState::StateType::ReadOnly);
  TestLoadBalancerContext lb_context1(&connection, Http::Headers::get().EnvoyOriginalDstHost.get(),
                                      "127.0.0.1:5555");

  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(server_context_.dispatcher_, post(_)).WillOnce([&post_cb](Event::PostCb cb) {
    post_cb = std::move(cb);
  });
  HostConstSharedPtr host1 = lb.chooseHost(&lb_context1);
  post_cb();
  ASSERT_NE(host1, nullptr);
  // Port override takes priority over filter state, so 6666->443
  EXPECT_EQ("10.10.11.11:443", host1->address()->asString());
}

TEST(DestinationAddress, ObjectFactory) {
  const std::string name = "envoy.network.transport_socket.original_dst_address";
  auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(name);
  ASSERT_NE(nullptr, factory);
  EXPECT_EQ(name, factory->name());
  const std::string address = "10.0.0.10:8080";
  auto object = factory->createFromBytes(address);
  ASSERT_NE(nullptr, object);
  EXPECT_EQ(address, object->serializeAsString());
  auto mirror = factory->reflect(object.get());
  ASSERT_NE(nullptr, mirror);
  EXPECT_THAT(mirror->getField("ip"), testing::VariantWith<absl::string_view>("10.0.0.10"));
  EXPECT_THAT(mirror->getField("port"), testing::VariantWith<int64_t>(8080));
  EXPECT_EQ(nullptr, factory->createFromBytes("foo"));
}

} // namespace
} // namespace Upstream
} // namespace Envoy
