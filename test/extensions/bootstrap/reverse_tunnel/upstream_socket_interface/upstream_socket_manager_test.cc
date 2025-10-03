#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class TestUpstreamSocketManager : public testing::Test {
protected:
  TestUpstreamSocketManager() {
    // Set up the stats scope
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));

    // Create the config
    config_.set_stat_prefix("test_prefix");

    // Create the socket interface
    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(context_);

    // Create the extension
    extension_ =
        std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config_);

    // Set up mock dispatcher with default expectations
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());

    // Create the socket manager with real extension
    socket_manager_ = std::make_unique<UpstreamSocketManager>(dispatcher_, extension_.get());
  }

  void TearDown() override {
    socket_manager_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  // Helper methods to access private members (friend class works for these methods)
  void verifyInitialState() {
    EXPECT_EQ(socket_manager_->accepted_reverse_connections_.size(), 0);
    EXPECT_EQ(socket_manager_->fd_to_node_map_.size(), 0);
    EXPECT_EQ(socket_manager_->node_to_cluster_map_.size(), 0);
    EXPECT_EQ(socket_manager_->cluster_to_node_map_.size(), 0);
  }

  bool verifyFDToNodeMap(int fd) {
    return socket_manager_->fd_to_node_map_.find(fd) != socket_manager_->fd_to_node_map_.end();
  }

  bool verifyFDToEventMap(int fd) {
    return socket_manager_->fd_to_event_map_.find(fd) != socket_manager_->fd_to_event_map_.end();
  }

  bool verifyFDToTimerMap(int fd) {
    return socket_manager_->fd_to_timer_map_.find(fd) != socket_manager_->fd_to_timer_map_.end();
  }

  size_t getFDToEventMapSize() { return socket_manager_->fd_to_event_map_.size(); }
  size_t getFDToTimerMapSize() { return socket_manager_->fd_to_timer_map_.size(); }

  size_t verifyAcceptedReverseConnectionsMap(const std::string& node_id) {
    auto it = socket_manager_->accepted_reverse_connections_.find(node_id);
    if (it == socket_manager_->accepted_reverse_connections_.end()) {
      return 0;
    }
    return it->second.size();
  }

  std::string getNodeToClusterMapping(const std::string& node_id) {
    auto it = socket_manager_->node_to_cluster_map_.find(node_id);
    if (it == socket_manager_->node_to_cluster_map_.end()) {
      return "";
    }
    return it->second;
  }

  std::vector<std::string> getClusterToNodeMapping(const std::string& cluster_id) {
    auto it = socket_manager_->cluster_to_node_map_.find(cluster_id);
    if (it == socket_manager_->cluster_to_node_map_.end()) {
      return {};
    }
    return it->second;
  }

  size_t getNodeToClusterMapSize() { return socket_manager_->node_to_cluster_map_.size(); }
  size_t getClusterToNodeMapSize() { return socket_manager_->cluster_to_node_map_.size(); }
  size_t getAcceptedReverseConnectionsSize() {
    return socket_manager_->accepted_reverse_connections_.size();
  }

  // Helper methods for the new test cases
  void addNodeToClusterMapping(const std::string& node_id, const std::string& cluster_id) {
    socket_manager_->node_to_cluster_map_[node_id] = cluster_id;
    socket_manager_->cluster_to_node_map_[cluster_id].push_back(node_id);
  }

  void addFDToNodeMapping(int fd, const std::string& node_id) {
    socket_manager_->fd_to_node_map_[fd] = node_id;
  }

  // Helper to create a mock socket with proper address setup
  Network::ConnectionSocketPtr createMockSocket(int fd = 123,
                                                const std::string& local_addr = "127.0.0.1:8080",
                                                const std::string& remote_addr = "127.0.0.1:9090") {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    // Parse local address (IP:port format)
    auto local_colon_pos = local_addr.find(':');
    std::string local_ip = local_addr.substr(0, local_colon_pos);
    uint32_t local_port = std::stoi(local_addr.substr(local_colon_pos + 1));
    auto local_address = Network::Utility::parseInternetAddressNoThrow(local_ip, local_port);

    // Parse remote address (IP:port format)
    auto remote_colon_pos = remote_addr.find(':');
    std::string remote_ip = remote_addr.substr(0, remote_colon_pos);
    uint32_t remote_port = std::stoi(remote_addr.substr(remote_colon_pos + 1));
    auto remote_address = Network::Utility::parseInternetAddressNoThrow(remote_ip, remote_port);

    // Create a mock IO handle and set it up
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    auto* mock_io_handle_ptr = mock_io_handle.get();
    EXPECT_CALL(*mock_io_handle_ptr, fdDoNotUse()).WillRepeatedly(Return(fd));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_ptr));

    // Store the mock_io_handle in the socket
    socket->io_handle_ = std::move(mock_io_handle);

    // Set up connection info provider with the desired addresses
    socket->connection_info_provider_->setLocalAddress(local_address);
    socket->connection_info_provider_->setRemoteAddress(remote_address);

    return socket;
  }

  // Helper to get sockets for a node
  std::list<Network::ConnectionSocketPtr>& getSocketsForNode(const std::string& node_id) {
    return socket_manager_->accepted_reverse_connections_[node_id];
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_;

  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  std::unique_ptr<UpstreamSocketManager> socket_manager_;
};

TEST_F(TestUpstreamSocketManager, CreateUpstreamSocketManager) {
  EXPECT_NE(socket_manager_, nullptr);
  auto socket_manager_no_extension = std::make_unique<UpstreamSocketManager>(dispatcher_, nullptr);
  EXPECT_NE(socket_manager_no_extension, nullptr);
}

TEST_F(TestUpstreamSocketManager, GetUpstreamExtension) {
  EXPECT_EQ(socket_manager_->getUpstreamExtension(), extension_.get());
  auto socket_manager_no_extension = std::make_unique<UpstreamSocketManager>(dispatcher_, nullptr);
  EXPECT_EQ(socket_manager_no_extension->getUpstreamExtension(), nullptr);
}

TEST_F(TestUpstreamSocketManager, AddConnectionSocketEmptyClusterId) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "";
  const std::chrono::seconds ping_interval(30);

  verifyInitialState();
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);
  verifyInitialState();
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved_socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, AddConnectionSocketEmptyNodeId) {
  auto socket = createMockSocket(456);
  const std::string node_id = "";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  verifyInitialState();
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);
  verifyInitialState();
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved_socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, AddAndGetMultipleSocketsSameNode) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  auto socket3 = createMockSocket(789);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  verifyInitialState();

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_TRUE(verifyFDToNodeMap(123));
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_TRUE(verifyFDToNodeMap(456));

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket3), ping_interval,
                                       false);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 3);
  EXPECT_TRUE(verifyFDToNodeMap(789));

  EXPECT_EQ(getFDToEventMapSize(), 3);
  EXPECT_EQ(getFDToTimerMapSize(), 3);

  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket1, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);

  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket2, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);

  auto retrieved_socket3 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket3, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);

  auto retrieved_socket4 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(retrieved_socket4, nullptr);
}

TEST_F(TestUpstreamSocketManager, AddAndGetSocketsMultipleNodes) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node1 = "node1";
  const std::string node2 = "node2";
  const std::string cluster1 = "cluster1";
  const std::string cluster2 = "cluster2";
  const std::chrono::seconds ping_interval(30);

  verifyInitialState();

  socket_manager_->addConnectionSocket(node1, cluster1, std::move(socket1), ping_interval, false);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node1), 1);
  EXPECT_EQ(getNodeToClusterMapping(node1), cluster1);

  socket_manager_->addConnectionSocket(node2, cluster2, std::move(socket2), ping_interval, false);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node1), 1);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node2), 1);
  EXPECT_EQ(getNodeToClusterMapping(node1), cluster1);
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster2);

  EXPECT_EQ(getFDToEventMapSize(), 2);
  EXPECT_EQ(getFDToTimerMapSize(), 2);

  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node1);
  EXPECT_NE(retrieved_socket1, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node1), 0);

  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node2);
  EXPECT_NE(retrieved_socket2, nullptr);
  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node2), 0);
}

TEST_F(TestUpstreamSocketManager, TestGetNodeID) {
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  auto socket1 = createMockSocket(123);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);

  std::string result_for_cluster = socket_manager_->getNodeID(cluster_id);
  EXPECT_EQ(result_for_cluster, node_id);

  std::string result_for_node = socket_manager_->getNodeID(node_id);
  EXPECT_EQ(result_for_node, node_id);

  const std::string non_existent_cluster = "non-existent-cluster";
  std::string result_for_non_existent = socket_manager_->getNodeID(non_existent_cluster);
  EXPECT_EQ(result_for_non_existent, non_existent_cluster);
}

TEST_F(TestUpstreamSocketManager, GetConnectionSocketEmpty) {
  auto socket = socket_manager_->getConnectionSocket("non-existent-node");
  EXPECT_EQ(socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, CleanStaleNodeEntryWithActiveSockets) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  EXPECT_EQ(getClusterToNodeMapping(cluster_id).size(), 1);

  socket_manager_->cleanStaleNodeEntry(node_id);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  EXPECT_EQ(getClusterToNodeMapping(cluster_id).size(), 1);
}

TEST_F(TestUpstreamSocketManager, CleanStaleNodeEntryClusterCleanup) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node1 = "node1";
  const std::string node2 = "node2";
  const std::string cluster_id = "shared-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node1, cluster_id, std::move(socket1), ping_interval, false);
  socket_manager_->addConnectionSocket(node2, cluster_id, std::move(socket2), ping_interval, false);

  EXPECT_EQ(getNodeToClusterMapping(node1), cluster_id);
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster_id);
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 2);
  EXPECT_EQ(getClusterToNodeMapSize(), 1);

  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node1);
  EXPECT_NE(retrieved_socket1, nullptr);

  EXPECT_EQ(getNodeToClusterMapping(node1), "");
  EXPECT_EQ(getNodeToClusterMapping(node2), cluster_id);
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1);
  EXPECT_EQ(cluster_nodes[0], node2);
  EXPECT_EQ(getClusterToNodeMapSize(), 1);

  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node2);
  EXPECT_NE(retrieved_socket2, nullptr);

  EXPECT_EQ(getNodeToClusterMapping(node1), "");
  EXPECT_EQ(getNodeToClusterMapping(node2), "");
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 0);
  EXPECT_EQ(getClusterToNodeMapSize(), 0);
}

TEST_F(TestUpstreamSocketManager, FileEventAndTimerCleanup) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  EXPECT_EQ(getFDToEventMapSize(), 2);
  EXPECT_EQ(getFDToTimerMapSize(), 2);

  auto retrieved_socket1 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket1, nullptr);

  EXPECT_FALSE(verifyFDToEventMap(123));
  EXPECT_FALSE(verifyFDToTimerMap(123));

  auto retrieved_socket2 = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket2, nullptr);

  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);
}

TEST_F(TestUpstreamSocketManager, MarkSocketNotPresentDead) {
  socket_manager_->markSocketDead(999);
  socket_manager_->markSocketDead(-1);
  socket_manager_->markSocketDead(0);
}

TEST_F(TestUpstreamSocketManager, MarkIdleSocketDead) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_TRUE(verifyFDToNodeMap(123));

  socket_manager_->markSocketDead(123);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToEventMap(123));
  EXPECT_FALSE(verifyFDToTimerMap(123));

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, MarkUsedSocketDead) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_TRUE(verifyFDToNodeMap(123));

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);

  socket_manager_->markSocketDead(123);

  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 0);
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadTriggerCleanup) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  auto cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 1);

  socket_manager_->markSocketDead(123);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  cluster_nodes = getClusterToNodeMapping(cluster_id);
  EXPECT_EQ(cluster_nodes.size(), 0);
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadMultipleSockets) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  auto socket3 = createMockSocket(789);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket3), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 3);
  EXPECT_EQ(getFDToEventMapSize(), 3);
  EXPECT_EQ(getFDToTimerMapSize(), 3);

  socket_manager_->markSocketDead(123);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
  EXPECT_EQ(getFDToEventMapSize(), 2);
  EXPECT_EQ(getFDToTimerMapSize(), 2);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToEventMap(123));
  EXPECT_FALSE(verifyFDToTimerMap(123));
  EXPECT_TRUE(verifyFDToNodeMap(456));
  EXPECT_TRUE(verifyFDToNodeMap(789));

  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);

  socket_manager_->markSocketDead(456);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_FALSE(verifyFDToNodeMap(456));
  EXPECT_FALSE(verifyFDToEventMap(456));
  EXPECT_FALSE(verifyFDToTimerMap(456));
  EXPECT_TRUE(verifyFDToNodeMap(789));

  socket_manager_->markSocketDead(789);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
  EXPECT_EQ(getFDToEventMapSize(), 0);
  EXPECT_EQ(getFDToTimerMapSize(), 0);
}

TEST_F(TestUpstreamSocketManager, PingConnectionsWriteSuccess) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);

  auto& sockets = getSocketsForNode(node_id);
  auto* mock_io_handle1 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.front()->ioHandle());
  auto* mock_io_handle2 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.back()->ioHandle());

  EXPECT_CALL(*mock_io_handle1, write(_))
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
      }));
  EXPECT_CALL(*mock_io_handle2, write(_))
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()};
      }));

  socket_manager_->pingConnections();

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);
}

TEST_F(TestUpstreamSocketManager, PingConnectionsWriteFailure) {
  auto socket1 = createMockSocket(123);
  auto socket2 = createMockSocket(456);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket1), ping_interval,
                                       false);
  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket2), ping_interval,
                                       false);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 2);

  auto& sockets = getSocketsForNode(node_id);
  auto* mock_io_handle1 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.front()->ioHandle());
  auto* mock_io_handle2 =
      dynamic_cast<NiceMock<Network::MockIoHandle>*>(&sockets.back()->ioHandle());

  EXPECT_CALL(*mock_io_handle1, write(_))
      .Times(1)
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::create(ECONNRESET)};
      }));

  socket_manager_->pingConnections(node_id);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 1);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_TRUE(verifyFDToNodeMap(456));
  EXPECT_EQ(getNodeToClusterMapping(node_id), cluster_id);
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 1);

  EXPECT_CALL(*mock_io_handle2, write(_))
      .Times(1)
      .WillRepeatedly(Invoke([](Buffer::Instance& buffer) -> Api::IoCallUint64Result {
        buffer.drain(buffer.length());
        return Api::IoCallUint64Result{0, Network::IoSocketError::create(EPIPE)};
      }));

  socket_manager_->pingConnections(node_id);

  EXPECT_EQ(verifyAcceptedReverseConnectionsMap(node_id), 0);
  EXPECT_FALSE(verifyFDToNodeMap(123));
  EXPECT_FALSE(verifyFDToNodeMap(456));
  EXPECT_EQ(getNodeToClusterMapping(node_id), "");
  EXPECT_EQ(getAcceptedReverseConnectionsSize(), 0);
}

TEST_F(TestUpstreamSocketManager, OnPingResponseValidResponse) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  const std::string ping_response = "RPING";
  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce([&](Buffer::Instance& buffer, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        buffer.add(ping_response);
        return Api::IoCallUint64Result{ping_response.size(), Api::IoError::none()};
      });

  socket_manager_->onPingResponse(*mock_io_handle);

  EXPECT_TRUE(verifyFDToNodeMap(123));
}

TEST_F(TestUpstreamSocketManager, OnPingResponseReadError) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce(
          Return(Api::IoCallUint64Result{0, Network::IoSocketError::getIoSocketEagainError()}));

  socket_manager_->onPingResponse(*mock_io_handle);

  EXPECT_FALSE(verifyFDToNodeMap(123));
}

TEST_F(TestUpstreamSocketManager, OnPingResponseConnectionClosed) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce(Return(Api::IoCallUint64Result{0, Api::IoError::none()}));

  socket_manager_->onPingResponse(*mock_io_handle);

  EXPECT_FALSE(verifyFDToNodeMap(123));
}

TEST_F(TestUpstreamSocketManager, OnPingResponseInvalidData) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
  EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));

  const std::string invalid_response = "INVALID_DATA";
  EXPECT_CALL(*mock_io_handle, read(_, _))
      .WillOnce([&](Buffer::Instance& buffer, absl::optional<uint64_t>) -> Api::IoCallUint64Result {
        buffer.add(invalid_response);
        return Api::IoCallUint64Result{invalid_response.size(), Api::IoError::none()};
      });

  // First invalid response should increment miss count but not immediately remove the fd.
  socket_manager_->onPingResponse(*mock_io_handle);
  EXPECT_TRUE(verifyFDToNodeMap(123));

  // Simulate two more timeouts to cross the default threshold (3).
  socket_manager_->onPingTimeout(123);
  socket_manager_->onPingTimeout(123);
  EXPECT_FALSE(verifyFDToNodeMap(123));
}

TEST_F(TestUpstreamSocketManager, GetConnectionSocketNoSocketsButValidMapping) {
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";

  addNodeToClusterMapping(node_id, cluster_id);

  auto socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_EQ(socket, nullptr);
}

TEST_F(TestUpstreamSocketManager, MarkSocketDeadInvalidSocketNotInPool) {
  auto socket = createMockSocket(123);
  const std::string node_id = "test-node";
  const std::string cluster_id = "test-cluster";
  const std::chrono::seconds ping_interval(30);

  socket_manager_->addConnectionSocket(node_id, cluster_id, std::move(socket), ping_interval,
                                       false);

  auto retrieved_socket = socket_manager_->getConnectionSocket(node_id);
  EXPECT_NE(retrieved_socket, nullptr);

  addFDToNodeMapping(123, node_id);

  socket_manager_->markSocketDead(123);

  EXPECT_FALSE(verifyFDToNodeMap(123));
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
