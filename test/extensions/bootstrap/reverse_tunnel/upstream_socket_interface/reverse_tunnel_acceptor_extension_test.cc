#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"

#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseTunnelAcceptorExtensionTest : public testing::Test {
protected:
  ReverseTunnelAcceptorExtensionTest() {
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    config_.set_stat_prefix("test_prefix");
    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(context_);
    extension_ =
        std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config_);
  }

  void setupThreadLocalSlot() {
    thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(dispatcher_, extension_.get());
    tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });
    extension_->tls_slot_ = std::move(tls_slot_);
    extension_->socket_interface_->extension_ = extension_.get();
  }

  void setupAnotherThreadLocalSlot() {
    another_thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(another_dispatcher_, extension_.get());
  }

  void TearDown() override {
    tls_slot_.reset();
    thread_local_registry_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;

  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<UpstreamSocketThreadLocal> thread_local_registry_;

  NiceMock<Event::MockDispatcher> another_dispatcher_{"worker_1"};
  std::shared_ptr<UpstreamSocketThreadLocal> another_thread_local_registry_;
};

TEST_F(ReverseTunnelAcceptorExtensionTest, InitializeWithDefaultStatPrefix) {
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface empty_config;

  auto extension_with_default =
      std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, empty_config);

  EXPECT_EQ(extension_with_default->statPrefix(), "upstream_reverse_connection");
}

TEST_F(ReverseTunnelAcceptorExtensionTest, InitializeWithCustomStatPrefix) {
  EXPECT_EQ(extension_->statPrefix(), "test_prefix");
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetStatsScope) {
  EXPECT_EQ(&extension_->getStatsScope(), stats_scope_.get());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, OnWorkerThreadInitialized) {
  extension_->onWorkerThreadInitialized();
}

TEST_F(ReverseTunnelAcceptorExtensionTest, OnServerInitializedSetsExtensionReference) {
  extension_->onServerInitialized();
  EXPECT_EQ(socket_interface_->getExtension(), extension_.get());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetLocalRegistryBeforeInitialization) {
  EXPECT_EQ(extension_->getLocalRegistry(), nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetLocalRegistryAfterInitialization) {
  setupThreadLocalSlot();

  auto* registry = extension_->getLocalRegistry();
  EXPECT_NE(registry, nullptr);

  auto* socket_manager = registry->socketManager();
  EXPECT_NE(socket_manager, nullptr);
  EXPECT_EQ(socket_manager->getUpstreamExtension(), extension_.get());

  const auto* const_registry = extension_->getLocalRegistry();
  EXPECT_NE(const_registry, nullptr);

  const auto* const_socket_manager = const_registry->socketManager();
  EXPECT_NE(const_socket_manager, nullptr);
  EXPECT_EQ(const_socket_manager->getUpstreamExtension(), extension_.get());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetPerWorkerStatMapSingleThread) {
  setupThreadLocalSlot();

  extension_->updatePerWorkerConnectionStats("node1", "cluster1", true);
  extension_->updatePerWorkerConnectionStats("node2", "cluster2", true);
  extension_->updatePerWorkerConnectionStats("node2", "cluster2", true);

  auto stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node1"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node2"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 2);

  for (const auto& [stat_name, value] : stat_map) {
    EXPECT_TRUE(stat_name.find("worker_0") != std::string::npos);
  }

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node1", "cluster1", true);

  stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node1"], 3);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 3);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node2"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 2);

  extension_->updatePerWorkerConnectionStats("node1", "cluster1", false);
  extension_->updatePerWorkerConnectionStats("node2", "cluster2", false);
  extension_->updatePerWorkerConnectionStats("node2", "cluster2", false);

  stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node1"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node2"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 0);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetCrossWorkerStatMapMultiThread) {
  setupThreadLocalSlot();
  setupAnotherThreadLocalSlot();

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node2", "cluster2", true);

  auto original_registry = thread_local_registry_;
  thread_local_registry_ = another_thread_local_registry_;

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node3", "cluster3", true);

  thread_local_registry_ = original_registry;

  auto stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node1"], 3);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node2"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node3"], 1);

  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster1"], 3);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster2"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster3"], 1);

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node2", "cluster2", false);

  stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node1"], 4);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster1"], 4);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node2"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster2"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.nodes.node3"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.clusters.cluster3"], 1);

  extension_->updatePerWorkerConnectionStats("node1", "cluster1", false);

  auto per_worker_stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.node.node1"], 3);
  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.cluster.cluster1"], 3);

  extension_->updateConnectionStats("node2", "cluster2", false);

  auto cross_worker_stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.clusters.cluster2"], 0);

  per_worker_stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.node.node2"], 0);
  EXPECT_EQ(per_worker_stat_map["test_scope.reverse_connections.worker_0.cluster.cluster2"], 0);

  thread_local_registry_ = another_thread_local_registry_;

  extension_->updatePerWorkerConnectionStats("node1", "cluster1", false);
  extension_->updatePerWorkerConnectionStats("node3", "cluster3", false);

  auto worker1_stat_map = extension_->getPerWorkerStatMap();

  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.node.node1"], 0);
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.cluster.cluster1"], 0);
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.node.node3"], 0);
  EXPECT_EQ(worker1_stat_map["test_scope.reverse_connections.worker_1.cluster.cluster3"], 0);

  thread_local_registry_ = original_registry;
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetConnectionStatsSyncMultiThread) {
  setupThreadLocalSlot();
  setupAnotherThreadLocalSlot();

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node2", "cluster2", true);

  auto original_registry = thread_local_registry_;
  thread_local_registry_ = another_thread_local_registry_;

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node3", "cluster3", true);

  thread_local_registry_ = original_registry;

  auto result = extension_->getConnectionStatsSync();
  auto& [connected_nodes, accepted_connections] = result;

  EXPECT_FALSE(connected_nodes.empty() || accepted_connections.empty());

  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "node1") !=
              connected_nodes.end());
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "node2") !=
              connected_nodes.end());
  EXPECT_TRUE(std::find(connected_nodes.begin(), connected_nodes.end(), "node3") !=
              connected_nodes.end());

  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster1") !=
              accepted_connections.end());
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster2") !=
              accepted_connections.end());
  EXPECT_TRUE(std::find(accepted_connections.begin(), accepted_connections.end(), "cluster3") !=
              accepted_connections.end());

  extension_->updateConnectionStats("node1", "cluster1", true);
  extension_->updateConnectionStats("node2", "cluster2", false);

  result = extension_->getConnectionStatsSync();
  auto& [updated_connected_nodes, updated_accepted_connections] = result;

  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "node2") ==
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster2") == updated_accepted_connections.end());

  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "node1") !=
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_connected_nodes.begin(), updated_connected_nodes.end(), "node3") !=
              updated_connected_nodes.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster1") != updated_accepted_connections.end());
  EXPECT_TRUE(std::find(updated_accepted_connections.begin(), updated_accepted_connections.end(),
                        "cluster3") != updated_accepted_connections.end());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, GetConnectionStatsSyncTimeout) {
  auto result = extension_->getConnectionStatsSync(std::chrono::milliseconds(1));

  auto& [connected_nodes, accepted_connections] = result;
  EXPECT_TRUE(connected_nodes.empty());
  EXPECT_TRUE(accepted_connections.empty());
}

TEST_F(ReverseTunnelAcceptorExtensionTest, IpFamilySupportIPv4) {
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET));
}

TEST_F(ReverseTunnelAcceptorExtensionTest, IpFamilySupportIPv6) {
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET6));
}

TEST_F(ReverseTunnelAcceptorExtensionTest, IpFamilySupportUnknown) {
  EXPECT_FALSE(socket_interface_->ipFamilySupported(AF_UNIX));
  EXPECT_FALSE(socket_interface_->ipFamilySupported(-1));
}

TEST_F(ReverseTunnelAcceptorExtensionTest, ExtensionNotInitialized) {
  ReverseTunnelAcceptor acceptor(context_);
  auto registry = acceptor.getLocalRegistry();
  EXPECT_EQ(registry, nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, CreateEmptyConfigProto) {
  auto proto = socket_interface_->createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);

  auto* typed_proto =
      dynamic_cast<envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
                       UpstreamReverseConnectionSocketInterface*>(proto.get());
  EXPECT_NE(typed_proto, nullptr);
}

TEST_F(ReverseTunnelAcceptorExtensionTest, FactoryName) {
  EXPECT_EQ(socket_interface_->name(), "envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
