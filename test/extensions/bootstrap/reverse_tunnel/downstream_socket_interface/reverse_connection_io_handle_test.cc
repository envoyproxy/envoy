#include <sys/socket.h>
#include <unistd.h>

#include <vector>

#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/tls/ssl_handshaker.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator_extension.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// Mock SslHandshakerImpl for testing SSL quiet shutdown functionality.
// This extends the real SslHandshakerImpl so dynamic_cast will succeed.
class MockSslHandshakerImpl : public Extensions::TransportSockets::Tls::SslHandshakerImpl {
public:
  // Constructor that takes an SSL object to pass to the base class.
  explicit MockSslHandshakerImpl(SSL* ssl)
      : Extensions::TransportSockets::Tls::SslHandshakerImpl(bssl::UniquePtr<SSL>(ssl), 0, nullptr),
        mock_ssl_(ssl) {}

  // Override ssl() to return our mock SSL pointer.
  SSL* ssl() const override { return mock_ssl_; }

private:
  SSL* mock_ssl_{nullptr};
};

// ReverseConnectionIOHandle Test Class.

class ReverseConnectionIOHandleTest : public testing::Test {
protected:
  ReverseConnectionIOHandleTest() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cluster_manager_));

    // Create the socket interface.
    socket_interface_ = std::make_unique<ReverseTunnelInitiator>(context_);

    // Set stat prefix to "reverse_connections" for tests.
    config_.set_stat_prefix("reverse_connections");
    // Enable detailed stats for tests that need per-node/cluster stats.
    config_.set_enable_detailed_stats(true);

    // Create the extension.
    extension_ = std::make_unique<ReverseTunnelInitiatorExtension>(context_, config_);

    // Set up mock dispatcher with default expectations.
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());
  }

  void TearDown() override {
    io_handle_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  // Helper to create a ReverseConnectionIOHandle with specified configuration.
  std::unique_ptr<ReverseConnectionIOHandle>
  createTestIOHandle(const ReverseConnectionSocketConfig& config,
                     ReverseTunnelInitiatorExtension* extension_override = nullptr) {
    // Create a test socket file descriptor.
    int test_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(test_fd, 0);

    // Create the IO handle.
    ReverseTunnelInitiatorExtension* extension_ptr =
        extension_override != nullptr ? extension_override : extension_.get();
    return std::make_unique<ReverseConnectionIOHandle>(test_fd, config, cluster_manager_,
                                                       extension_ptr, *stats_scope_);
  }

  // Helper to create a default test configuration.
  ReverseConnectionSocketConfig createDefaultTestConfig() {
    ReverseConnectionSocketConfig config;
    config.src_cluster_id = "test-cluster";
    config.src_node_id = "test-node";
    config.enable_circuit_breaker = true;
    config.remote_clusters.push_back(RemoteClusterConnectionConfig("remote-cluster", 2));
    return config;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelInitiator> socket_interface_;
  std::unique_ptr<ReverseTunnelInitiatorExtension> extension_;
  std::unique_ptr<ReverseConnectionIOHandle> io_handle_;

  // Mock cluster manager.
  NiceMock<Upstream::MockClusterManager> cluster_manager_;

  // Thread local components for testing.
  std::unique_ptr<ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<DownstreamSocketThreadLocal> thread_local_registry_;
  std::unique_ptr<ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>> another_tls_slot_;
  std::shared_ptr<DownstreamSocketThreadLocal> another_thread_local_registry_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);

  // Mock socket for testing.
  std::unique_ptr<Network::ConnectionSocket> mock_socket_;

  // Thread Local Setup Helpers.

  // Helper function to set up thread local slot for tests.
  void setupThreadLocalSlot() {
    // Create a thread local registry.
    thread_local_registry_ =
        std::make_shared<DownstreamSocketThreadLocal>(dispatcher_, *stats_scope_);

    // Create the actual TypedSlot.
    tls_slot_ = ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the slot to return our registry.
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Set the slot in the extension using the test-only method.
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));
  }

  // Multi-Thread Local Setup Helpers.

  void setupAnotherThreadLocalSlot() {
    // Create a thread local registry for the other dispatcher.
    another_thread_local_registry_ =
        std::make_shared<DownstreamSocketThreadLocal>(dispatcher_, *stats_scope_);

    // Create the actual TypedSlot.
    another_tls_slot_ =
        ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the slot to return our registry.
    another_tls_slot_->set(
        [registry = another_thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Set the slot in the extension using the test-only method.
    extension_->setTestOnlyTLSRegistry(std::move(another_tls_slot_));
  }

  // Trigger Pipe Management Helpers.

  bool isTriggerPipeReady() const { return io_handle_->isTriggerPipeReady(); }

  void createTriggerPipe() { io_handle_->createTriggerPipe(); }

  int getTriggerPipeReadFd() const { return io_handle_->trigger_pipe_read_fd_; }

  int getTriggerPipeWriteFd() const { return io_handle_->trigger_pipe_write_fd_; }

  // Connection Management Helpers.

  void addConnectionToEstablishedQueue(Network::ClientConnectionPtr connection) {
    io_handle_->established_connections_.push(std::move(connection));
  }

  bool initiateOneReverseConnection(const std::string& cluster_name,
                                    const std::string& host_address,
                                    Upstream::HostConstSharedPtr host) {
    return io_handle_->initiateOneReverseConnection(cluster_name, host_address, host);
  }

  void maintainReverseConnections() { io_handle_->maintainReverseConnections(); }

  void maintainClusterConnections(const std::string& cluster_name,
                                  const RemoteClusterConnectionConfig& cluster_config) {
    io_handle_->maintainClusterConnections(cluster_name, cluster_config);
  }

  // Host Management Helpers.

  void maybeUpdateHostsMappingsAndConnections(const std::string& cluster_id,
                                              const std::vector<std::string>& hosts) {
    io_handle_->maybeUpdateHostsMappingsAndConnections(cluster_id, hosts);
  }

  bool shouldAttemptConnectionToHost(const std::string& host_address,
                                     const std::string& cluster_name) {
    return io_handle_->shouldAttemptConnectionToHost(host_address, cluster_name);
  }

  void trackConnectionFailure(const std::string& host_address, const std::string& cluster_name) {
    io_handle_->trackConnectionFailure(host_address, cluster_name);
  }

  void resetHostBackoff(const std::string& host_address) {
    io_handle_->resetHostBackoff(host_address);
  }

  // Data Access Helpers.

  const absl::flat_hash_map<std::string, ReverseConnectionIOHandle::HostConnectionInfo>&
  getHostToConnInfoMap() const {
    return io_handle_->host_to_conn_info_map_;
  }

  const ReverseConnectionIOHandle::HostConnectionInfo&
  getHostConnectionInfo(const std::string& host_address) const {
    auto it = io_handle_->host_to_conn_info_map_.find(host_address);
    EXPECT_NE(it, io_handle_->host_to_conn_info_map_.end())
        << "Host " << host_address << " not found in host_to_conn_info_map_";
    return it->second;
  }

  ReverseConnectionIOHandle::HostConnectionInfo&
  getMutableHostConnectionInfo(const std::string& host_address) {
    auto it = io_handle_->host_to_conn_info_map_.find(host_address);
    EXPECT_NE(it, io_handle_->host_to_conn_info_map_.end())
        << "Host " << host_address << " not found in host_to_conn_info_map_";
    return it->second;
  }

  const std::vector<std::unique_ptr<RCConnectionWrapper>>& getConnectionWrappers() const {
    return io_handle_->connection_wrappers_;
  }

  const absl::flat_hash_map<RCConnectionWrapper*, std::string>& getConnWrapperToHostMap() const {
    return io_handle_->conn_wrapper_to_host_map_;
  }

  // Test Data Setup Helpers.

  void addHostConnectionInfo(const std::string& host_address, const std::string& cluster_name,
                             uint32_t target_count) {
    io_handle_->host_to_conn_info_map_[host_address] =
        ReverseConnectionIOHandle::HostConnectionInfo{
            host_address,
            cluster_name,
            {},           // connection_keys - empty set initially
            target_count, // target_connection_count
            0,            // failure_count
            // last_failure_time
            std::chrono::steady_clock::now(), // NO_CHECK_FORMAT(real_time)
            // backoff_until - set to epoch start so host is not in backoff initially
            std::chrono::steady_clock::time_point{}, // NO_CHECK_FORMAT(real_time)
            {}                                       // connection_states
        };
  }

  // Helper to create a mock host.
  Upstream::HostConstSharedPtr createMockHost(const std::string& address) {
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
    auto mock_address = std::make_shared<Network::Address::Ipv4Instance>(address, 8080);
    EXPECT_CALL(*mock_host, address()).WillRepeatedly(Return(mock_address));
    return mock_host;
  }

  // Helper to create a mock host with a pipe address (no IP/port).
  Upstream::HostConstSharedPtr createMockPipeHost(const std::string& path) {
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
    auto status_or_pipe = Network::Address::PipeInstance::create(path);
    auto owned = std::move(status_or_pipe.value());
    std::shared_ptr<Network::Address::PipeInstance> shared_pipe(std::move(owned));
    Network::Address::InstanceConstSharedPtr mock_address = shared_pipe;
    EXPECT_CALL(*mock_host, address()).WillRepeatedly(Return(mock_address));
    return mock_host;
  }

  // Helper method to set up mock connection with proper socket expectations.
  std::unique_ptr<NiceMock<Network::MockClientConnection>> setupMockConnection() {
    auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();

    // Create a mock socket for the connection.
    auto mock_socket_ptr = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();

    // Set up IO handle expectations.
    EXPECT_CALL(*mock_io_handle, resetFileEvents()).WillRepeatedly(Return());
    EXPECT_CALL(*mock_io_handle, isOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_io_handle, duplicate()).WillRepeatedly(Invoke([]() {
      auto duplicated_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
      EXPECT_CALL(*duplicated_handle, isOpen()).WillRepeatedly(Return(true));
      return duplicated_handle;
    }));

    // Set up socket expectations.
    EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));
    EXPECT_CALL(*mock_socket_ptr, isOpen()).WillRepeatedly(Return(true));

    // Store the mock_io_handle in the socket before casting.
    mock_socket_ptr->io_handle_ = std::move(mock_io_handle);

    // Cast the mock to the base ConnectionSocket type and store it in member variable.
    mock_socket_ = std::unique_ptr<Network::ConnectionSocket>(mock_socket_ptr.release());

    // Set up connection expectations for getSocket()
    EXPECT_CALL(*mock_connection, getSocket()).WillRepeatedly(ReturnRef(mock_socket_));

    return mock_connection;
  }

  // Helper to access private members for testing.
  void addWrapperToHostMap(RCConnectionWrapper* wrapper, const std::string& host_address) {
    io_handle_->conn_wrapper_to_host_map_[wrapper] = host_address;
  }

  void cleanup() { io_handle_->cleanup(); }

  void removeStaleHostAndCloseConnections(const std::string& host) {
    io_handle_->removeStaleHostAndCloseConnections(host);
  }

  // Helper to get the established connections queue size (if accessible)
  size_t getEstablishedConnectionsSize() const {
    return io_handle_->established_connections_.size();
  }
};

// Test getClusterManager returns correct reference.
TEST_F(ReverseConnectionIOHandleTest, GetClusterManager) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Verify that getClusterManager returns the correct reference.
  EXPECT_EQ(&io_handle_->getClusterManager(), &cluster_manager_);
}

// Basic setup.
TEST_F(ReverseConnectionIOHandleTest, BasicSetup) {
  // Test that constructor doesn't crash and creates a valid instance.
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Verify the IO handle has a valid file descriptor.
  EXPECT_GE(io_handle_->fdDoNotUse(), 0);
}

TEST_F(ReverseConnectionIOHandleTest, RequestPathDefaultsAndOverrides) {
  auto default_config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(default_config);
  ASSERT_NE(io_handle_, nullptr);
  EXPECT_EQ(io_handle_->requestPath(),
            ReverseConnectionUtility::DEFAULT_REVERSE_TUNNEL_REQUEST_PATH);

  ReverseConnectionSocketConfig custom_config = createDefaultTestConfig();
  custom_config.request_path = "/custom/handshake";
  auto custom_handle = createTestIOHandle(custom_config);
  ASSERT_NE(custom_handle, nullptr);
  EXPECT_EQ(custom_handle->requestPath(), "/custom/handshake");
}

// listen() is a no-op for the initiator
TEST_F(ReverseConnectionIOHandleTest, ListenNoOp) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Test that listen() returns success (0) with no error.
  auto result = io_handle_->listen(10);
  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

// Test isTriggerPipeReady() behavior.
TEST_F(ReverseConnectionIOHandleTest, IsTriggerPipeReady) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Initially, trigger pipe should not be ready.
  EXPECT_FALSE(isTriggerPipeReady());

  // Create the trigger pipe.
  createTriggerPipe();

  // Now trigger pipe should be ready.
  EXPECT_TRUE(isTriggerPipeReady());

  // Verify the file descriptors are valid.
  EXPECT_GE(getTriggerPipeReadFd(), 0);
  EXPECT_GE(getTriggerPipeWriteFd(), 0);
}

// Test createTriggerPipe() basic pipe creation.
TEST_F(ReverseConnectionIOHandleTest, CreateTriggerPipe) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Initially, trigger pipe should not be ready.
  EXPECT_FALSE(isTriggerPipeReady());

  // Manually call createTriggerPipe.
  createTriggerPipe();

  // Verify that the trigger pipe was created successfully.
  EXPECT_TRUE(isTriggerPipeReady());
  EXPECT_GE(getTriggerPipeReadFd(), 0);
  EXPECT_GE(getTriggerPipeWriteFd(), 0);

  // Verify getPipeMonitorFd returns the correct file descriptor.
  EXPECT_EQ(io_handle_->getPipeMonitorFd(), getTriggerPipeReadFd());

  // Verify the file descriptors are different.
  EXPECT_NE(getTriggerPipeReadFd(), getTriggerPipeWriteFd());
}

// Test initializeFileEvent() creates trigger pipe.
TEST_F(ReverseConnectionIOHandleTest, InitializeFileEventCreatesTriggerPipe) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Initially, trigger pipe should not be ready.
  EXPECT_FALSE(isTriggerPipeReady());

  // Mock file event callback.
  Event::FileReadyCb mock_callback = [](uint32_t) -> absl::Status { return absl::OkStatus(); };

  // Call initializeFileEvent - this should create the trigger pipe.
  io_handle_->initializeFileEvent(dispatcher_, mock_callback, Event::FileTriggerType::Level,
                                  Event::FileReadyType::Read);

  // Verify that the trigger pipe was created successfully.
  EXPECT_TRUE(isTriggerPipeReady());
  EXPECT_GE(getTriggerPipeReadFd(), 0);
  EXPECT_GE(getTriggerPipeWriteFd(), 0);

  // Verify getPipeMonitorFd returns the correct file descriptor.
  EXPECT_EQ(io_handle_->getPipeMonitorFd(), getTriggerPipeReadFd());
}

// Test that subsequent calls to initializeFileEvent do not create new pipes.
TEST_F(ReverseConnectionIOHandleTest, InitializeFileEventDoesNotCreateNewPipes) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Initially, trigger pipe should not be ready.
  EXPECT_FALSE(isTriggerPipeReady());

  // Mock file event callback.
  Event::FileReadyCb mock_callback = [](uint32_t) -> absl::Status { return absl::OkStatus(); };

  // First call to initializeFileEvent - should create the trigger pipe.
  io_handle_->initializeFileEvent(dispatcher_, mock_callback, Event::FileTriggerType::Level,
                                  Event::FileReadyType::Read);

  // Verify that the trigger pipe was created.
  EXPECT_TRUE(isTriggerPipeReady());
  int first_read_fd = getTriggerPipeReadFd();
  int first_write_fd = getTriggerPipeWriteFd();
  EXPECT_GE(first_read_fd, 0);
  EXPECT_GE(first_write_fd, 0);

  // Second call to initializeFileEvent - should NOT create new pipes because.
  // is_reverse_conn_started_ is true
  io_handle_->initializeFileEvent(dispatcher_, mock_callback, Event::FileTriggerType::Level,
                                  Event::FileReadyType::Read);

  // Verify that the same file descriptors are still used (no new pipes created)
  EXPECT_TRUE(isTriggerPipeReady());
  EXPECT_EQ(getTriggerPipeReadFd(), first_read_fd);
  EXPECT_EQ(getTriggerPipeWriteFd(), first_write_fd);

  // Verify getPipeMonitorFd still returns the correct file descriptor.
  EXPECT_EQ(io_handle_->getPipeMonitorFd(), first_read_fd);
}

// Test that we do NOT update stats for the cluster if src_node_id is empty.
TEST_F(ReverseConnectionIOHandleTest, EmptySrcNodeIdNoStatsUpdate) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  // Create config with empty src_node_id.
  ReverseConnectionSocketConfig empty_node_config;
  empty_node_config.src_cluster_id = "test-cluster";
  empty_node_config.src_node_id = ""; // Empty node ID
  empty_node_config.remote_clusters.push_back(RemoteClusterConnectionConfig("remote-cluster", 2));

  io_handle_ = createTestIOHandle(empty_node_config);
  EXPECT_NE(io_handle_, nullptr);

  // Call maintainReverseConnections - should return early due to empty src_node_id.
  maintainReverseConnections();

  // Verify that no stats were updated.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map.size(), 0); // No stats should be created
}

// Test that rev_conn_retry_timer_ gets created and enabled upon calling initializeFileEvent.
TEST_F(ReverseConnectionIOHandleTest, RetryTimerEnabled) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Mock timer expectations.
  auto mock_timer = new NiceMock<Event::MockTimer>();
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Return(mock_timer));
  EXPECT_CALL(*mock_timer, enableTimer(_, _));

  // Mock file event callback.
  Event::FileReadyCb mock_callback = [](uint32_t) -> absl::Status { return absl::OkStatus(); };

  // Call initializeFileEvent - this should create and enable the retry timer.
  io_handle_->initializeFileEvent(dispatcher_, mock_callback, Event::FileTriggerType::Level,
                                  Event::FileReadyType::Read);
}

// Test that rev_conn_retry_timer_ is properly managed when reverse connection is started.
TEST_F(ReverseConnectionIOHandleTest, RetryTimerWhenReverseConnStarted) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Mock timer expectations.
  auto mock_timer = new NiceMock<Event::MockTimer>();
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillOnce(Return(mock_timer));
  EXPECT_CALL(*mock_timer, enableTimer(_, _));

  // Mock file event callback.
  Event::FileReadyCb mock_callback = [](uint32_t) -> absl::Status { return absl::OkStatus(); };

  // Call initializeFileEvent to create the timer.
  io_handle_->initializeFileEvent(dispatcher_, mock_callback, Event::FileTriggerType::Level,
                                  Event::FileReadyType::Read);

  // Call initializeFileEvent again to ensure the timer is not created again.
  io_handle_->initializeFileEvent(dispatcher_, mock_callback, Event::FileTriggerType::Level,
                                  Event::FileReadyType::Read);
}

// Test that we do not initiate reverse tunnels when thread local cluster is not present.
TEST_F(ReverseConnectionIOHandleTest, NoThreadLocalClusterCannotConnect) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up cluster manager to return nullptr for non-existent cluster.
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("non-existent-cluster"))
      .WillOnce(Return(nullptr));

  // Call maintainClusterConnections with non-existent cluster.
  RemoteClusterConnectionConfig cluster_config("non-existent-cluster", 2);
  maintainClusterConnections("non-existent-cluster", cluster_config);

  // Verify that CannotConnect gauge was updated for the cluster.
  auto stat_map = extension_->getCrossWorkerStatMap();

  for (const auto& stat : stat_map) {
    std::cout << stat.first << " " << stat.second << std::endl;
  }
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.non-existent-cluster.cannot_connect"],
            1);
}

// Test that we do not initiate reverse tunnels when cluster has no hosts.
TEST_F(ReverseConnectionIOHandleTest, NoHostsInClusterCannotConnect) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster with empty host map.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("empty-cluster"))
      .WillOnce(Return(mock_thread_local_cluster.get()));

  // Set up empty priority set.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Set up empty cross priority host map.
  auto empty_host_map = std::make_shared<Upstream::HostMap>();
  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(empty_host_map));

  // Call maintainClusterConnections with empty cluster.
  RemoteClusterConnectionConfig cluster_config("empty-cluster", 2);
  maintainClusterConnections("empty-cluster", cluster_config);

  // Verify that CannotConnect gauge was updated for the cluster.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.empty-cluster.cannot_connect"], 1);
}

// Test maybeUpdateHostsMappingsAndConnections with valid hosts.
TEST_F(ReverseConnectionIOHandleTest, MaybeUpdateHostsMappingsValidHosts) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with some hosts.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host1 = createMockHost("192.168.1.1");
  auto mock_host2 = createMockHost("192.168.1.2");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host1);
  (*host_map)["192.168.1.2"] = std::const_pointer_cast<Upstream::Host>(mock_host2);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Call maintainClusterConnections which will create HostConnectionInfo entries and call.
  // maybeUpdateHostsMappingsAndConnections
  RemoteClusterConnectionConfig cluster_config("test-cluster", 2);
  maintainClusterConnections("test-cluster", cluster_config);

  // Verify that hosts were added to the mapping.
  const auto& host_to_conn_info_map = getHostToConnInfoMap();
  EXPECT_EQ(host_to_conn_info_map.size(), 2);
  EXPECT_NE(host_to_conn_info_map.find("192.168.1.1"), host_to_conn_info_map.end());
  EXPECT_NE(host_to_conn_info_map.find("192.168.1.2"), host_to_conn_info_map.end());
}

// Test maybeUpdateHostsMappingsAndConnections with no new hosts.
TEST_F(ReverseConnectionIOHandleTest, MaybeUpdateHostsMappingsNoNewHosts) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with multiple hosts.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host1 = createMockHost("192.168.1.1");
  auto mock_host2 = createMockHost("192.168.1.2");
  auto mock_host3 = createMockHost("192.168.1.3");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host1);
  (*host_map)["192.168.1.2"] = std::const_pointer_cast<Upstream::Host>(mock_host2);
  (*host_map)["192.168.1.3"] = std::const_pointer_cast<Upstream::Host>(mock_host3);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Call maintainClusterConnections which will create HostConnectionInfo entries and call.
  // maybeUpdateHostsMappingsAndConnections
  RemoteClusterConnectionConfig cluster_config("test-cluster", 2);
  maintainClusterConnections("test-cluster", cluster_config);

  // Verify that all three host entries exist after maintainClusterConnections.
  const auto& host_to_conn_info_map = getHostToConnInfoMap();
  EXPECT_EQ(host_to_conn_info_map.size(), 3);
  EXPECT_NE(host_to_conn_info_map.find("192.168.1.1"), host_to_conn_info_map.end());
  EXPECT_NE(host_to_conn_info_map.find("192.168.1.2"), host_to_conn_info_map.end());
  EXPECT_NE(host_to_conn_info_map.find("192.168.1.3"), host_to_conn_info_map.end());

  // Now test partial host removal by calling maybeUpdateHostsMappingsAndConnections with fewer.
  // hosts
  std::vector<std::string> reduced_host_addresses = {"192.168.1.1", "192.168.1.3"};
  maybeUpdateHostsMappingsAndConnections("test-cluster", reduced_host_addresses);

  // Verify that the removed host was cleaned up but others remain.
  const auto& updated_host_to_conn_info_map = getHostToConnInfoMap();
  EXPECT_EQ(updated_host_to_conn_info_map.size(), 2);
  EXPECT_NE(updated_host_to_conn_info_map.find("192.168.1.1"), updated_host_to_conn_info_map.end());
  EXPECT_EQ(updated_host_to_conn_info_map.find("192.168.1.2"),
            updated_host_to_conn_info_map.end()); // Should be removed
  EXPECT_NE(updated_host_to_conn_info_map.find("192.168.1.3"), updated_host_to_conn_info_map.end());
}

// Test shouldAttemptConnectionToHost with valid host and no existing connections.
TEST_F(ReverseConnectionIOHandleTest, ShouldAttemptConnectionToHostValidHost) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Call maintainClusterConnections to create HostConnectionInfo entries.
  RemoteClusterConnectionConfig cluster_config("test-cluster", 2);
  maintainClusterConnections("test-cluster", cluster_config);

  // Test with valid host and no existing connections.
  bool should_attempt = shouldAttemptConnectionToHost("192.168.1.1", "test-cluster");
  EXPECT_TRUE(should_attempt);

  // Test circuit breaker disabled scenario - should always return true regardless of backoff state.
  // First, put the host in backoff by tracking a failure.
  trackConnectionFailure("192.168.1.1", "test-cluster");

  // Verify host is in backoff with circuit breaker enabled (default).
  EXPECT_FALSE(shouldAttemptConnectionToHost("192.168.1.1", "test-cluster"));

  // Now create a new IO handle with circuit breaker disabled.
  auto config_disabled = createDefaultTestConfig();
  config_disabled.enable_circuit_breaker = false;
  auto io_handle_disabled = createTestIOHandle(config_disabled);
  EXPECT_NE(io_handle_disabled, nullptr);

  // Set up the same thread local cluster for the new IO handle.
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));
  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Call maintainClusterConnections to create HostConnectionInfo entries in the new IO handle.
  maintainClusterConnections("test-cluster", cluster_config);

  // Put the host in backoff in the new IO handle.
  io_handle_disabled->trackConnectionFailure("192.168.1.1", "test-cluster");

  // With circuit breaker disabled, shouldAttemptConnectionToHost should always return true.
  EXPECT_TRUE(io_handle_disabled->shouldAttemptConnectionToHost("192.168.1.1", "test-cluster"));
}

// Test trackConnectionFailure puts host in backoff.
TEST_F(ReverseConnectionIOHandleTest, TrackConnectionFailurePutsHostInBackoff) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // First call maintainClusterConnections to create HostConnectionInfo entries.
  RemoteClusterConnectionConfig cluster_config("test-cluster", 2);
  maintainClusterConnections("test-cluster", cluster_config);

  // Verify host is initially not in backoff.
  bool should_attempt_before = shouldAttemptConnectionToHost("192.168.1.1", "test-cluster");
  EXPECT_TRUE(should_attempt_before);

  // Call trackConnectionFailure to put host in backoff.
  trackConnectionFailure("192.168.1.1", "test-cluster");

  // Verify host is now in backoff.
  bool should_attempt_after = shouldAttemptConnectionToHost("192.168.1.1", "test-cluster");
  EXPECT_FALSE(should_attempt_after);

  // Verify stat gauges - should show backoff state.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.backoff"], 1);

  // Test that trackConnectionFailure returns if host_to_conn_info_map_ does not have an entry.
  // Call trackConnectionFailure with a host that doesn't exist in host_to_conn_info_map_
  trackConnectionFailure("non-existent-host", "test-cluster");

  // Verify that no stats were updated since the host doesn't exist.
  auto stat_map_after_non_existent = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(
      stat_map_after_non_existent["test_scope.reverse_connections.host.non-existent-host.backoff"],
      0);

  // Test that maintainClusterConnections skips hosts in backoff.
  // Call maintainClusterConnections again - should skip the host in backoff.
  // and not attempt any new connections
  maintainClusterConnections("test-cluster", cluster_config);

  // Verify that the host is still in backoff state.
  EXPECT_FALSE(shouldAttemptConnectionToHost("192.168.1.1", "test-cluster"));
}

// Test resetHostBackoff resets the backoff.
TEST_F(ReverseConnectionIOHandleTest, ResetHostBackoff) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // First call maintainClusterConnections to create HostConnectionInfo entries.
  RemoteClusterConnectionConfig cluster_config("test-cluster", 2);
  maintainClusterConnections("test-cluster", cluster_config);

  // Verify host is initially not in backoff.
  bool should_attempt_before = shouldAttemptConnectionToHost("192.168.1.1", "test-cluster");
  EXPECT_TRUE(should_attempt_before);

  // Call trackConnectionFailure to put host in backoff.
  trackConnectionFailure("192.168.1.1", "test-cluster");

  // Verify host is now in backoff.
  bool should_attempt_after_failure = shouldAttemptConnectionToHost("192.168.1.1", "test-cluster");
  EXPECT_FALSE(should_attempt_after_failure);

  // Verify stat gauges - should show backoff state.
  auto stat_map_after_failure = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map_after_failure["test_scope.reverse_connections.host.192.168.1.1.backoff"], 1);

  // Call resetHostBackoff to reset the backoff.
  resetHostBackoff("192.168.1.1");

  // Verify host is no longer in backoff.
  bool should_attempt_after_reset = shouldAttemptConnectionToHost("192.168.1.1", "test-cluster");
  EXPECT_TRUE(should_attempt_after_reset);

  // Verify stat gauges - should show recovered state.
  auto stat_map_after_reset = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map_after_reset["test_scope.reverse_connections.host.192.168.1.1.backoff"], 0);
  EXPECT_EQ(stat_map_after_reset["test_scope.reverse_connections.host.192.168.1.1.recovered"], 1);
}

// Test resetHostBackoff returns if host_to_conn_info_map_ does not have an entry.
TEST_F(ReverseConnectionIOHandleTest, ResetHostBackoffReturnsIfHostNotFound) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Call resetHostBackoff with a host that doesn't exist in host_to_conn_info_map_
  // This should not crash and should return early.
  resetHostBackoff("non-existent-host");

  // Verify that no stats were updated since the host doesn't exist.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.non-existent-host.recovered"], 0);
}

// Test trackConnectionFailure exponential backoff.
TEST_F(ReverseConnectionIOHandleTest, TrackConnectionFailureExponentialBackoff) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // First call maintainClusterConnections to create HostConnectionInfo entries.
  RemoteClusterConnectionConfig cluster_config("test-cluster", 2);
  maintainClusterConnections("test-cluster", cluster_config);

  // Get initial host info.
  const auto& host_info_initial = getHostConnectionInfo("192.168.1.1");
  EXPECT_EQ(host_info_initial.failure_count, 0);

  // First failure - should have 1 second backoff (1000ms)
  trackConnectionFailure("192.168.1.1", "test-cluster");
  const auto& host_info_1 = getHostConnectionInfo("192.168.1.1");
  EXPECT_EQ(host_info_1.failure_count, 1);
  // Verify backoff_until is set to a future time (approximately current_time + 1000ms)
  auto backoff_duration_1 =
      host_info_1.backoff_until - std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)
  // backoff_delay_ms = 1000 * 2^(1-1) = 1000 * 2^0 = 1000 * 1 = 1000ms
  auto backoff_ms_1 =
      std::chrono::duration_cast<std::chrono::milliseconds>(backoff_duration_1).count();
  EXPECT_GE(backoff_ms_1, 900);  // Should be at least 900ms (allowing for small timing variations)
  EXPECT_LE(backoff_ms_1, 1100); // Should be at most 1100ms

  // Second failure - should have 2 second backoff (2000ms)
  trackConnectionFailure("192.168.1.1", "test-cluster");
  const auto& host_info_2 = getHostConnectionInfo("192.168.1.1");
  EXPECT_EQ(host_info_2.failure_count, 2);
  // backoff_delay_ms = 1000 * 2^(2-1) = 1000 * 2^1 = 1000 * 2 = 2000ms
  auto backoff_duration_2 =
      host_info_2.backoff_until - std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)
  auto backoff_ms_2 =
      std::chrono::duration_cast<std::chrono::milliseconds>(backoff_duration_2).count();
  EXPECT_GE(backoff_ms_2, 1900); // Should be at least 1900ms
  EXPECT_LE(backoff_ms_2, 2100); // Should be at most 2100ms

  // Third failure - should have 4 second backoff (4000ms)
  trackConnectionFailure("192.168.1.1", "test-cluster");
  const auto& host_info_3 = getHostConnectionInfo("192.168.1.1");
  EXPECT_EQ(host_info_3.failure_count, 3);
  // backoff_delay_ms = 1000 * 2^(3-1) = 1000 * 2^2 = 1000 * 4 = 4000ms
  auto backoff_duration_3 =
      host_info_3.backoff_until - std::chrono::steady_clock::now(); // NO_CHECK_FORMAT(real_time)
  auto backoff_ms_3 =
      std::chrono::duration_cast<std::chrono::milliseconds>(backoff_duration_3).count();
  EXPECT_GE(backoff_ms_3, 3900); // Should be at least 3900ms
  EXPECT_LE(backoff_ms_3, 4100); // Should be at most 4100ms

  // Verify that shouldAttemptConnectionToHost returns false during backoff.
  EXPECT_FALSE(shouldAttemptConnectionToHost("192.168.1.1", "test-cluster"));
}

// Test host mapping and backoff integration.
TEST_F(ReverseConnectionIOHandleTest, HostMappingAndBackoffIntegration) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster for cluster-A.
  auto mock_thread_local_cluster_a = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("cluster-A"))
      .WillRepeatedly(Return(mock_thread_local_cluster_a.get()));

  // Set up priority set with hosts for cluster-A.
  auto mock_priority_set_a = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster_a, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set_a));

  // Create host map for cluster-A with hosts A1, A2, A3.
  auto host_map_a = std::make_shared<Upstream::HostMap>();
  auto mock_host_a1 = createMockHost("192.168.1.1");
  auto mock_host_a2 = createMockHost("192.168.1.2");
  auto mock_host_a3 = createMockHost("192.168.1.3");
  (*host_map_a)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host_a1);
  (*host_map_a)["192.168.1.2"] = std::const_pointer_cast<Upstream::Host>(mock_host_a2);
  (*host_map_a)["192.168.1.3"] = std::const_pointer_cast<Upstream::Host>(mock_host_a3);

  EXPECT_CALL(*mock_priority_set_a, crossPriorityHostMap()).WillRepeatedly(Return(host_map_a));

  // Set up mock thread local cluster for cluster-B.
  auto mock_thread_local_cluster_b = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("cluster-B"))
      .WillRepeatedly(Return(mock_thread_local_cluster_b.get()));

  // Set up priority set with hosts for cluster-B.
  auto mock_priority_set_b = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster_b, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set_b));

  // Create host map for cluster-B with hosts B1, B2.
  auto host_map_b = std::make_shared<Upstream::HostMap>();
  auto mock_host_b1 = createMockHost("192.168.2.1");
  auto mock_host_b2 = createMockHost("192.168.2.2");
  (*host_map_b)["192.168.2.1"] = std::const_pointer_cast<Upstream::Host>(mock_host_b1);
  (*host_map_b)["192.168.2.2"] = std::const_pointer_cast<Upstream::Host>(mock_host_b2);

  EXPECT_CALL(*mock_priority_set_b, crossPriorityHostMap()).WillRepeatedly(Return(host_map_b));

  // Step 1: Create initial host mappings for cluster-A.
  RemoteClusterConnectionConfig cluster_config_a("cluster-A", 2);
  maintainClusterConnections("cluster-A", cluster_config_a);

  // Step 2: Create initial host mappings for cluster-B.
  RemoteClusterConnectionConfig cluster_config_b("cluster-B", 2);
  maintainClusterConnections("cluster-B", cluster_config_b);

  // Verify all hosts exist initially.
  const auto& host_to_conn_info_map_initial = getHostToConnInfoMap();
  EXPECT_EQ(host_to_conn_info_map_initial.size(),
            5); // 192.168.1.1, 192.168.1.2, 192.168.1.3, 192.168.2.1, 192.168.2.2
  EXPECT_NE(host_to_conn_info_map_initial.find("192.168.1.1"), host_to_conn_info_map_initial.end());
  EXPECT_NE(host_to_conn_info_map_initial.find("192.168.1.2"), host_to_conn_info_map_initial.end());
  EXPECT_NE(host_to_conn_info_map_initial.find("192.168.1.3"), host_to_conn_info_map_initial.end());
  EXPECT_NE(host_to_conn_info_map_initial.find("192.168.2.1"), host_to_conn_info_map_initial.end());
  EXPECT_NE(host_to_conn_info_map_initial.find("192.168.2.2"), host_to_conn_info_map_initial.end());

  // Step 3: Put some hosts in backoff.
  trackConnectionFailure("192.168.1.1", "cluster-A"); // 192.168.1.1 in backoff
  trackConnectionFailure("192.168.2.1", "cluster-B"); // 192.168.2.1 in backoff
  // 192.168.1.2, 192.168.1.3, 192.168.2.2 remain normal

  // Verify backoff states.
  EXPECT_FALSE(shouldAttemptConnectionToHost("192.168.1.1", "cluster-A")); // In backoff
  EXPECT_FALSE(shouldAttemptConnectionToHost("192.168.2.1", "cluster-B")); // In backoff
  EXPECT_TRUE(shouldAttemptConnectionToHost("192.168.1.2", "cluster-A"));  // Normal
  EXPECT_TRUE(shouldAttemptConnectionToHost("192.168.1.3", "cluster-A"));  // Normal
  EXPECT_TRUE(shouldAttemptConnectionToHost("192.168.2.2", "cluster-B"));  // Normal

  // Step 4: Update host mappings.
  // - Move 192.168.1.2 from cluster-A to cluster-B
  // - Remove 192.168.1.3 from cluster-A
  // - Add new host 192.168.1.4 to cluster-A
  maybeUpdateHostsMappingsAndConnections(
      "cluster-A", {"192.168.1.1", "192.168.1.4"}); // 192.168.1.2, 192.168.1.3 removed
  maybeUpdateHostsMappingsAndConnections(
      "cluster-B", {"192.168.2.1", "192.168.2.2", "192.168.1.2"}); // 192.168.1.2 added

  // Step 5: Verify backoff states are preserved for existing hosts.
  EXPECT_FALSE(shouldAttemptConnectionToHost("192.168.1.1", "cluster-A")); // Still in backoff
  EXPECT_FALSE(shouldAttemptConnectionToHost("192.168.2.1", "cluster-B")); // Still in backoff

  // Step 6: Verify moved host has clean state.
  EXPECT_TRUE(shouldAttemptConnectionToHost("192.168.1.2", "cluster-B")); // Moved, no backoff

  // Step 7: Verify removed host is cleaned up.
  const auto& host_to_conn_info_map_after = getHostToConnInfoMap();
  EXPECT_EQ(host_to_conn_info_map_after.find("192.168.1.3"),
            host_to_conn_info_map_after.end()); // Removed

  // Step 8: Verify stats are updated correctly.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.backoff"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.2.1.backoff"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.2.backoff"],
            0); // Reset when moved
}

// Test initiateOneReverseConnection when connection establishment fails.
TEST_F(ReverseConnectionIOHandleTest, InitiateOneReverseConnectionFailure) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // First call maintainClusterConnections to create HostConnectionInfo entries.
  RemoteClusterConnectionConfig cluster_config("test-cluster", 2);
  maintainClusterConnections("test-cluster", cluster_config);

  // Mock tcpConn to return null connection (simulating connection failure)
  Upstream::MockHost::MockCreateConnectionData failed_conn_data;
  failed_conn_data.connection_ = nullptr; // Connection creation failed
  failed_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(failed_conn_data));

  // Call initiateOneReverseConnection - should fail.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_FALSE(result);

  // Verify that CannotConnect stats are set.
  // Calculation: 3 increments total.
  // - 2 increments from maintainClusterConnections (target_connection_count = 2)
  // - 1 increment from our direct call to initiateOneReverseConnection
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.cannot_connect"], 3);
}

// Test initiateOneReverseConnection when connection establishment is successful.
TEST_F(ReverseConnectionIOHandleTest, InitiateOneReverseConnectionSuccess) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Create HostConnectionInfo entry using helper method.
  addHostConnectionInfo("192.168.1.1", "test-cluster", 1);

  // Set up mock for successful connection.
  auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection - should succeed.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify that Connecting stats are set.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connecting"], 1);

  // Verify that connection wrapper is added to the map.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);
}

// Test that reverse connection initiation works with custom stat scope.
TEST_F(ReverseConnectionIOHandleTest, InitiateReverseConnectionWithCustomScope) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  // Create config with custom stat prefix.
  ReverseConnectionSocketConfig custom_prefix_config;
  custom_prefix_config.src_cluster_id = "test-cluster";
  custom_prefix_config.src_node_id = "test-node";
  custom_prefix_config.remote_clusters.push_back(RemoteClusterConnectionConfig("test-cluster", 1));

  // Create a new extension with custom stat prefix.
  envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface custom_config;
  custom_config.set_stat_prefix("custom_stats");
  custom_config.set_enable_detailed_stats(true);

  auto custom_extension =
      std::make_unique<ReverseTunnelInitiatorExtension>(context_, custom_config);
  custom_extension->setTestOnlyTLSRegistry(std::move(tls_slot_));

  // Replace the class member io_handle_ with our custom one for this test
  auto original_io_handle = std::move(io_handle_);
  io_handle_ = std::make_unique<ReverseConnectionIOHandle>(8, // dummy fd
                                                           custom_prefix_config, cluster_manager_,
                                                           custom_extension.get(), *stats_scope_);

  // Initialize the file event to set up worker_dispatcher_ properly.
  Event::FileReadyCb mock_callback = [](uint32_t) { return absl::OkStatus(); };
  io_handle_->initializeFileEvent(dispatcher_, mock_callback, Event::FileTriggerType::Level,
                                  Event::FileReadyType::Read);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Create HostConnectionInfo entry using helper method.
  addHostConnectionInfo("192.168.1.1", "test-cluster", 1);

  // Set up mock for successful connection.
  auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection using the helper method - should succeed.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify that Connecting stats are set with custom stat prefix.
  auto stat_map = custom_extension->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.custom_stats.host.192.168.1.1.connecting"], 1);

  // Restore the original io_handle_
  io_handle_ = std::move(original_io_handle);
}

// Test maintainClusterConnections skips hosts that already have enough connections.
TEST_F(ReverseConnectionIOHandleTest, MaintainClusterConnectionsSkipsHostsWithEnoughConnections) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // First call maintainClusterConnections to create HostConnectionInfo entries.
  RemoteClusterConnectionConfig cluster_config("test-cluster", 1); // Only need 1 connection
  maintainClusterConnections("test-cluster", cluster_config);

  // Manually add a connection key to simulate having enough connections.
  const auto& host_info = getHostConnectionInfo("192.168.1.1");
  EXPECT_EQ(host_info.connection_keys.size(), 0); // Initially no connections

  // Manually add a connection key to the host info to simulate having enough connections.
  auto& mutable_host_info = getMutableHostConnectionInfo("192.168.1.1");
  mutable_host_info.connection_keys.insert("fake-connection-key");

  // Verify we now have enough connections.
  EXPECT_EQ(getHostConnectionInfo("192.168.1.1").connection_keys.size(), 1);

  // Call maintainClusterConnections again - should skip the host since it has enough connections.
  maintainClusterConnections("test-cluster", cluster_config);

  // Verify that no additional connection attempts were made.
  // The host should still have exactly 1 connection.
  EXPECT_EQ(getHostConnectionInfo("192.168.1.1").connection_keys.size(), 1);
}

// Test initiateOneReverseConnection with empty host address.
TEST_F(ReverseConnectionIOHandleTest, InitiateOneReverseConnectionEmptyHostAddress) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Call initiateOneReverseConnection with empty host address - should fail.
  bool result = initiateOneReverseConnection("test-cluster", "", nullptr);
  EXPECT_FALSE(result);

  // When host address is empty, only cluster stats are updated.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.cannot_connect"], 1);
}

// Test initiateOneReverseConnection with non-existent cluster.
TEST_F(ReverseConnectionIOHandleTest, InitiateOneReverseConnectionNonExistentCluster) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up cluster manager to return nullptr for non-existent cluster.
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("non-existent-cluster"))
      .WillOnce(Return(nullptr));

  // Call initiateOneReverseConnection with non-existent cluster - should fail.
  bool result = initiateOneReverseConnection("non-existent-cluster", "192.168.1.1", nullptr);
  EXPECT_FALSE(result);

  // When cluster is not found, both host and cluster stats are updated.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.cannot_connect"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.non-existent-cluster.cannot_connect"],
            1);

  // No wrapper should be created since the cluster doesn't exist.
  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 0);
}

// Test mixed success and failure scenarios for multiple connection attempts.
TEST_F(ReverseConnectionIOHandleTest, InitiateMultipleConnectionsMixedResults) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with hosts.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host1 = createMockHost("192.168.1.1");
  auto mock_host2 = createMockHost("192.168.1.2");
  auto mock_host3 = createMockHost("192.168.1.3");

  // MockHostDescription already has a cluster_ member that's returned by cluster().
  // We don't need to set up expectations for it.
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host1);
  (*host_map)["192.168.1.2"] = std::const_pointer_cast<Upstream::Host>(mock_host2);
  (*host_map)["192.168.1.3"] = std::const_pointer_cast<Upstream::Host>(mock_host3);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Create HostConnectionInfo entries for all hosts with target count of 3.
  addHostConnectionInfo("192.168.1.1", "test-cluster", 1); // Host 1
  addHostConnectionInfo("192.168.1.2", "test-cluster", 1); // Host 2
  addHostConnectionInfo("192.168.1.3", "test-cluster", 1); // Host 3

  // Set up connection outcomes in sequence:
  // 1. First host: successful connection
  // 2. Second host: null connection (failure)
  // 3. Third host: successful connection

  // Prepare mock connections that will be transferred to the wrappers.
  auto mock_connection1 = std::make_unique<NiceMock<Network::MockClientConnection>>();
  auto mock_connection3 = std::make_unique<NiceMock<Network::MockClientConnection>>();

  // Set up connection info for the connections.
  auto local_address = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.2", 40000);
  auto remote_address1 = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.1", 8080);
  auto remote_address3 = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.3", 8080);

  // Set up local/remote addresses for connections using the stream_info_.
  mock_connection1->stream_info_.downstream_connection_info_provider_->setLocalAddress(
      local_address);
  mock_connection1->stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address1);

  mock_connection3->stream_info_.downstream_connection_info_provider_->setLocalAddress(
      local_address);
  mock_connection3->stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      remote_address3);

  // Set up expectations on the mock connections before they're moved.
  // Set up connection expectations for mock_connection1.
  EXPECT_CALL(*mock_connection1, id()).WillRepeatedly(Return(1));
  EXPECT_CALL(*mock_connection1, state()).WillRepeatedly(Return(Network::Connection::State::Open));
  EXPECT_CALL(*mock_connection1, addConnectionCallbacks(_));
  EXPECT_CALL(*mock_connection1, connect());
  EXPECT_CALL(*mock_connection1, addReadFilter(_));
  EXPECT_CALL(*mock_connection1, write(_, _))
      .Times(1)
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool) -> void {
        // Drain the buffer to simulate actual write.
        buffer.drain(buffer.length());
      }));
  // Expect calls during shutdown.
  EXPECT_CALL(*mock_connection1, removeConnectionCallbacks(_)).Times(testing::AtMost(1));
  EXPECT_CALL(*mock_connection1, close(_)).Times(testing::AtMost(1));

  // Set up connection expectations for mock_connection3.
  EXPECT_CALL(*mock_connection3, id()).WillRepeatedly(Return(3));
  EXPECT_CALL(*mock_connection3, state()).WillRepeatedly(Return(Network::Connection::State::Open));
  EXPECT_CALL(*mock_connection3, addConnectionCallbacks(_));
  EXPECT_CALL(*mock_connection3, connect());
  EXPECT_CALL(*mock_connection3, addReadFilter(_));
  EXPECT_CALL(*mock_connection3, write(_, _))
      .Times(1)
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool) -> void {
        // Drain the buffer to simulate actual write.
        buffer.drain(buffer.length());
      }));
  // Expect calls during shutdown.
  EXPECT_CALL(*mock_connection3, removeConnectionCallbacks(_)).Times(testing::AtMost(1));
  EXPECT_CALL(*mock_connection3, close(_)).Times(testing::AtMost(1));

  // Set up connection attempts with host-specific expectations.
  // We need to transfer ownership of the connections properly.
  // The lambda will be called multiple times, so we use a counter to track which call we're on.
  int call_count = 0;
  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_))
      .WillRepeatedly(
          testing::Invoke([&call_count, &mock_connection1, &mock_connection3, mock_host1,
                           mock_host2, mock_host3](Upstream::LoadBalancerContext* context)
                              -> Upstream::MockHost::MockCreateConnectionData {
            auto* reverse_context =
                dynamic_cast<ReverseConnection::ReverseConnectionLoadBalancerContext*>(context);
            EXPECT_NE(reverse_context, nullptr);

            auto override_host = reverse_context->overrideHostToSelect();
            EXPECT_TRUE(override_host.has_value());

            std::string host_address = std::string(override_host->first);

            Upstream::MockHost::MockCreateConnectionData result;
            if (host_address == "192.168.1.1") {
              // First host: success - transfer ownership of mock_connection1
              // Transfer ownership only on the first call for this host.
              if (mock_connection1) {
                result.connection_ = mock_connection1.release();
              } else {
                result.connection_ = nullptr; // Already used.
              }
              result.host_description_ = mock_host1;
            } else if (host_address == "192.168.1.2") {
              // Second host: failure - no connection
              result.connection_ = nullptr;
              result.host_description_ = mock_host2;
            } else if (host_address == "192.168.1.3") {
              // Third host: success - transfer ownership of mock_connection3
              // Transfer ownership only on the first call for this host.
              if (mock_connection3) {
                result.connection_ = mock_connection3.release();
              } else {
                result.connection_ = nullptr; // Already used.
              }
              result.host_description_ = mock_host3;
            } else {
              // Unexpected host.
              EXPECT_TRUE(false) << "Unexpected host address: " << host_address;
              result.connection_ = nullptr;
              result.host_description_ = mock_host2;
            }
            call_count++;
            return result;
          }));

  // Create 1 connection per host.
  RemoteClusterConnectionConfig cluster_config("test-cluster", 1);

  // Call maintainClusterConnections which will attempt connections to all hosts.
  maintainClusterConnections("test-cluster", cluster_config);

  // Verify final stats.
  auto stat_map = extension_->getCrossWorkerStatMap();

  // Verify connecting stats for successful connections.
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connecting"], 1); // Success
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.3.connecting"], 1); // Success

  // Verify cannot_connect stats for failed connection.
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.2.cannot_connect"],
            1); // Failed

  // Verify cluster-level stats for test-cluster.
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.connecting"],
            2); // 2 successful connections
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.cannot_connect"],
            1); // 1 failed connection

  // Verify that only 2 connection wrappers were created (for successful connections)
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 2);

  // Verify that wrappers are mapped to successful hosts only.
  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 2);

  // Count hosts in the mapping.
  std::set<std::string> mapped_hosts;
  for (const auto& [wrapper, host] : wrapper_to_host_map) {
    mapped_hosts.insert(host);
  }
  EXPECT_EQ(mapped_hosts.size(), 2);                               // Should have 2 successful hosts
  EXPECT_NE(mapped_hosts.find("192.168.1.1"), mapped_hosts.end()); // Success
  EXPECT_EQ(mapped_hosts.find("192.168.1.2"), mapped_hosts.end()); // Failed - not in map
  EXPECT_NE(mapped_hosts.find("192.168.1.3"), mapped_hosts.end()); // Success
}

// Test removeStaleHostAndCloseConnections removes host and closes connections.
TEST_F(ReverseConnectionIOHandleTest, RemoveStaleHostAndCloseConnections) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with multiple hosts.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host1 = createMockHost("192.168.1.1");
  auto mock_host2 = createMockHost("192.168.1.2");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host1);
  (*host_map)["192.168.1.2"] = std::const_pointer_cast<Upstream::Host>(mock_host2);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Set up successful connections for both hosts.
  auto mock_connection1 = std::make_unique<NiceMock<Network::MockClientConnection>>();
  Upstream::MockHost::MockCreateConnectionData success_conn_data1;
  success_conn_data1.connection_ = mock_connection1.get();
  success_conn_data1.host_description_ = mock_host1;

  auto mock_connection2 = std::make_unique<NiceMock<Network::MockClientConnection>>();
  Upstream::MockHost::MockCreateConnectionData success_conn_data2;
  success_conn_data2.connection_ = mock_connection2.get();
  success_conn_data2.host_description_ = mock_host2;

  // Set up connection attempts with host-specific expectations.
  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_))
      .WillRepeatedly(testing::Invoke([&](Upstream::LoadBalancerContext* context) {
        // Cast to our custom context to get the host address.
        auto* reverse_context =
            dynamic_cast<ReverseConnection::ReverseConnectionLoadBalancerContext*>(context);
        EXPECT_NE(reverse_context, nullptr);

        auto override_host = reverse_context->overrideHostToSelect();
        EXPECT_TRUE(override_host.has_value());

        std::string host_address = std::string(override_host->first);

        if (host_address == "192.168.1.1") {
          return success_conn_data1; // First host: success
        } else if (host_address == "192.168.1.2") {
          return success_conn_data2; // Second host: success
        } else {
          // Unexpected host.
          EXPECT_TRUE(false) << "Unexpected host address: " << host_address;
          return success_conn_data1; // Default fallback
        }
      }));

  mock_connection1.release();
  mock_connection2.release();

  // First call maintainClusterConnections to create HostConnectionInfo entries and connection.
  // wrappers
  RemoteClusterConnectionConfig cluster_config("test-cluster", 1);
  maintainClusterConnections("test-cluster", cluster_config);

  // Verify both hosts are initially present.
  EXPECT_EQ(getHostToConnInfoMap().size(), 2);
  EXPECT_NE(getHostToConnInfoMap().find("192.168.1.1"), getHostToConnInfoMap().end());
  EXPECT_NE(getHostToConnInfoMap().find("192.168.1.2"), getHostToConnInfoMap().end());

  // Verify that connection wrappers were created by maintainClusterConnections.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 2); // One wrapper per host
  EXPECT_EQ(getConnWrapperToHostMap().size(), 2);

  // Call removeStaleHostAndCloseConnections to remove host 192.168.1.1
  removeStaleHostAndCloseConnections("192.168.1.1");

  // Verify that host 192.168.1.1 is still in host_to_conn_info_map_
  // (removeStaleHostAndCloseConnections doesn't remove it)
  EXPECT_EQ(getHostToConnInfoMap().size(), 2);
  EXPECT_NE(getHostToConnInfoMap().find("192.168.1.1"), getHostToConnInfoMap().end());
  EXPECT_NE(getHostToConnInfoMap().find("192.168.1.2"), getHostToConnInfoMap().end());

  // Verify that connection wrappers for the removed host are removed.
  EXPECT_EQ(getConnectionWrappers().size(), 1);   // Only host 192.168.1.2's wrapper remains
  EXPECT_EQ(getConnWrapperToHostMap().size(), 1); // Only host 192.168.1.2's mapping remains

  // Verify that host 192.168.1.2's wrapper is still present and unaffected
  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);
  EXPECT_EQ(wrapper_to_host_map.begin()->second, "192.168.1.2"); // Only 192.168.1.2 should remain
}

// Test read() method - should delegate to base class.
TEST_F(ReverseConnectionIOHandleTest, ReadMethod) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Create a buffer to read into.
  Buffer::OwnedImpl buffer;

  // Call read() - should delegate to base class implementation.
  auto result = io_handle_->read(buffer, absl::optional<uint64_t>(100));

  // Should return a valid result.
  EXPECT_NE(result.err_, nullptr);
}

// Test write() method - should delegate to base class.
TEST_F(ReverseConnectionIOHandleTest, WriteMethod) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Create a buffer to write from.
  Buffer::OwnedImpl buffer;
  buffer.add("test data");

  // Call write() - should delegate to base class implementation.
  auto result = io_handle_->write(buffer);

  // Should return a valid result.
  EXPECT_NE(result.err_, nullptr);
}

// Test connect() method - should delegate to base class.
TEST_F(ReverseConnectionIOHandleTest, ConnectMethod) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Create a mock address.
  auto address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080);

  // Call connect() - should delegate to base class implementation.
  auto result = io_handle_->connect(address);

  // Should return a valid result.
  EXPECT_NE(result.errno_, 0); // Should fail since we're not actually connecting
}

// Test onEvent() method - should delegate to base class.
TEST_F(ReverseConnectionIOHandleTest, OnEventMethod) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Call onEvent() with a mock event - no-op.
  io_handle_->onEvent(Network::ConnectionEvent::LocalClose);
}

// Test RCConnectionWrapper::onEvent with null connection.
TEST_F(ReverseConnectionIOHandleTest, RCConnectionWrapperOnEventWithNullConnection) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Release the connection to make it null.
  wrapper.releaseConnection();

  // Call onEvent with RemoteClose event - should handle null connection gracefully.
  wrapper.onEvent(Network::ConnectionEvent::RemoteClose);
}

// onConnectionDone Unit Tests

// Early returns in onConnectionDone without calling initiateOneReverseConnection.
TEST_F(ReverseConnectionIOHandleTest, OnConnectionDoneEarlyReturns) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Test 1.1: Null wrapper - should return early
  io_handle_->onConnectionDone("test error", nullptr, false);

  // Verify no stats were updated.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map.size(), 0);

  // Test 1.2: Empty conn_wrapper_to_host_map_ - should return early
  // Create a dummy wrapper pointer (we can't easily mock RCConnectionWrapper directly)
  RCConnectionWrapper* wrapper_ptr = reinterpret_cast<RCConnectionWrapper*>(0x12345678);

  // Verify the map is empty.
  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 0);

  io_handle_->onConnectionDone("test error", wrapper_ptr, false);

  // Verify no stats were updated.
  stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map.size(), 0);

  // Test 1.3: Empty host_to_conn_info_map_ - should return early after finding wrapper
  // First add wrapper to the map but no host info.
  addWrapperToHostMap(wrapper_ptr, "192.168.1.1");

  // Verify host info map is empty.
  const auto& host_to_conn_info_map = getHostToConnInfoMap();
  EXPECT_EQ(host_to_conn_info_map.size(), 0);

  io_handle_->onConnectionDone("test error", wrapper_ptr, false);

  // Verify wrapper was removed from map but no stats updated.
  EXPECT_EQ(getConnWrapperToHostMap().size(), 0);
  stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map.size(), 0);
}

// Connection success scenario - test stats and wrapper creation and mapping.
TEST_F(ReverseConnectionIOHandleTest, OnConnectionDoneSuccess) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Create trigger pipe BEFORE initiating connection to ensure it's ready.
  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Create HostConnectionInfo entry.
  addHostConnectionInfo("192.168.1.1", "test-cluster", 1);

  // Create a successful connection.
  auto mock_connection = setupMockConnection();

  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection to create the wrapper.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify wrapper was created and mapped.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();
  EXPECT_EQ(wrapper_to_host_map.at(wrapper_ptr), "192.168.1.1");

  // Verify initial state - no established connections yet.
  EXPECT_EQ(getEstablishedConnectionsSize(), 0);

  // Call onConnectionDone to simulate successful connection completion.
  io_handle_->onConnectionDone("reverse connection accepted", wrapper_ptr, false);

  // Verify wrapper was removed from tracking (cleanup should happen)
  EXPECT_EQ(getConnWrapperToHostMap().size(), 0);
  EXPECT_EQ(getConnectionWrappers().size(), 0);

  // Verify that connection was pushed to established_connections_
  EXPECT_EQ(getEstablishedConnectionsSize(), 1);

  // Verify that trigger mechanism was executed.
  // Read 1 byte from the pipe to verify the trigger was written.
  char trigger_byte;
  int pipe_read_fd = getTriggerPipeReadFd();
  EXPECT_GE(pipe_read_fd, 0);

  ssize_t bytes_read = ::read(pipe_read_fd, &trigger_byte, 1);
  EXPECT_EQ(bytes_read, 1) << "Expected to read 1 byte from trigger pipe, got " << bytes_read;
  EXPECT_EQ(trigger_byte, 1) << "Expected trigger byte to be 1, got "
                             << static_cast<int>(trigger_byte);
}

// Success path where trigger write fails: still enqueues connection and cleans up wrapper.
TEST_F(ReverseConnectionIOHandleTest, OnConnectionDoneSuccessTriggerWriteFailure) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Prepare trigger pipe, then close write end so ::write fails.
  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());
  ::close(getTriggerPipeWriteFd());

  // Mock cluster and single host.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);
  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  addHostConnectionInfo("192.168.1.1", "test-cluster", 1);

  auto mock_connection = setupMockConnection();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;
  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));
  mock_connection.release();

  // Create wrapper via initiation, then complete as success.
  EXPECT_TRUE(initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host));
  RCConnectionWrapper* wrapper_ptr = getConnectionWrappers()[0].get();
  io_handle_->onConnectionDone("reverse connection accepted", wrapper_ptr, false);

  // Even though trigger write failed, connection should be queued for accept.
  EXPECT_EQ(getEstablishedConnectionsSize(), 1);
}

// Internal address with zero hosts should early fail and update CannotConnect state.
TEST_F(ReverseConnectionIOHandleTest, InitiateOneReverseConnectionInternalAddressNoHosts) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Provide non-null info and an empty host set to yield host_count == 0.
  auto mock_cluster_info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(*mock_thread_local_cluster, info()).WillRepeatedly(Return(mock_cluster_info));
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));
  std::vector<Upstream::HostSetPtr> host_sets; // empty
  EXPECT_CALL(*mock_priority_set, hostSetsPerPriority()).WillRepeatedly(ReturnRef(host_sets));

  auto mock_host = createMockPipeHost("/tmp/rev.sock");
  bool ok = initiateOneReverseConnection("test-cluster", "envoy://internal", mock_host);
  EXPECT_FALSE(ok);
}

// Pipe address host exercises the log branch that prints address without a port.
TEST_F(ReverseConnectionIOHandleTest, InitiateOneReverseConnectionLogsWithoutPort) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));
  auto host_map = std::make_shared<Upstream::HostMap>();
  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  auto mock_host = createMockPipeHost("/tmp/rev.sock");
  auto mock_connection = setupMockConnection();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;
  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));
  mock_connection.release();

  bool created = initiateOneReverseConnection("test-cluster", "10.0.0.9", mock_host);
  EXPECT_TRUE(created);
}

// Test 3: Connection failure and recovery scenario.
TEST_F(ReverseConnectionIOHandleTest, OnConnectionDoneFailureAndRecovery) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Create HostConnectionInfo entry.
  addHostConnectionInfo("192.168.1.1", "test-cluster", 1);

  // Step 1: Create initial connection.
  auto mock_connection1 = setupMockConnection();

  Upstream::MockHost::MockCreateConnectionData success_conn_data1;
  success_conn_data1.connection_ = mock_connection1.get();
  success_conn_data1.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data1));

  mock_connection1.release();

  // Call initiateOneReverseConnection to create the wrapper.
  bool result1 = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result1);

  // Get the wrapper.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();
  EXPECT_EQ(wrapper_to_host_map.at(wrapper_ptr), "192.168.1.1");

  // Verify host and cluster stats after connection initiation.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connecting"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.connecting"], 1);

  // Step 2: Simulate connection failure by calling onConnectionDone with error.
  io_handle_->onConnectionDone("connection timeout", wrapper_ptr, true);

  // Verify wrapper was removed from tracking maps after failure.
  EXPECT_EQ(getConnWrapperToHostMap().size(), 0);
  EXPECT_EQ(getConnectionWrappers().size(), 0);

  // Verify failure stats - onConnectionDone should have called trackConnectionFailure.
  stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.failed"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.backoff"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connecting"],
            0); // Should be decremented
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.failed"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.backoff"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.connecting"],
            0); // Should be decremented

  // Verify host is now in backoff.
  EXPECT_FALSE(shouldAttemptConnectionToHost("192.168.1.1", "test-cluster"));

  // Step 3: Create a new connection for recovery.
  auto mock_connection2 = setupMockConnection();

  Upstream::MockHost::MockCreateConnectionData success_conn_data2;
  success_conn_data2.connection_ = mock_connection2.get();
  success_conn_data2.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data2));

  mock_connection2.release();

  // Call initiateOneReverseConnection again for recovery.
  bool result2 = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result2);

  // Verify new wrapper was created and mapped.
  const auto& connection_wrappers2 = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers2.size(), 1);

  const auto& wrapper_to_host_map2 = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map2.size(), 1);

  RCConnectionWrapper* wrapper_ptr2 = connection_wrappers2[0].get();
  EXPECT_EQ(wrapper_to_host_map2.at(wrapper_ptr2), "192.168.1.1");

  // Verify stats after recovery connection initiation.
  stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connecting"],
            1); // New connection
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.connecting"],
            1); // New connection
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.backoff"],
            0); // Reset by initiateOneReverseConnection
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.backoff"],
            0); // Reset by initiateOneReverseConnection
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.recovered"],
            1); // Recovery recorded
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.recovered"],
            1); // Recovery recorded

  // Step 4: Simulate connection success (recovery) by calling onConnectionDone with success.
  io_handle_->onConnectionDone("reverse connection accepted", wrapper_ptr2, false);

  // Verify wrapper was removed from tracking maps after success.
  EXPECT_EQ(getConnWrapperToHostMap().size(), 0);
  EXPECT_EQ(getConnectionWrappers().size(), 0);

  // Verify recovery stats - onConnectionDone should have called resetHostBackoff.
  stat_map = extension_->getCrossWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connected"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.recovered"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.backoff"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connecting"],
            0); // Should be decremented
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.failed"],
            0); // Reset by initiateOneReverseConnection
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.connected"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.recovered"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.backoff"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.connecting"],
            0); // Should be decremented
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.failed"],
            0); // Reset by initiateOneReverseConnection

  // Verify host is no longer in backoff.
  EXPECT_TRUE(shouldAttemptConnectionToHost("192.168.1.1", "test-cluster"));

  // Verify final state - all maps should be clean.
  EXPECT_EQ(getConnWrapperToHostMap().size(), 0);
  EXPECT_EQ(getConnectionWrappers().size(), 0);

  // Verify host info is still present (should not be removed)
  const auto& host_to_conn_info_map = getHostToConnInfoMap();
  EXPECT_EQ(host_to_conn_info_map.size(), 1);
  EXPECT_NE(host_to_conn_info_map.find("192.168.1.1"), host_to_conn_info_map.end());
}

// Test downstream connection closure and re-initiation.
TEST_F(ReverseConnectionIOHandleTest, OnDownstreamConnectionClosedTriggersReInitiation) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Create trigger pipe BEFORE initiating connection to ensure it's ready.
  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  // Create HostConnectionInfo entry.
  addHostConnectionInfo("192.168.1.1", "test-cluster", 1);

  // Step 1: Create initial connection.
  auto mock_connection = setupMockConnection();

  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection to create the wrapper.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify wrapper was created and mapped.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();
  EXPECT_EQ(wrapper_to_host_map.at(wrapper_ptr), "192.168.1.1");

  // Verify initial state - no established connections yet.
  EXPECT_EQ(getEstablishedConnectionsSize(), 0);

  // Step 2: Simulate successful connection completion.
  io_handle_->onConnectionDone("reverse connection accepted", wrapper_ptr, false);

  // Verify wrapper was removed from tracking (cleanup should happen)
  EXPECT_EQ(getConnWrapperToHostMap().size(), 0);
  EXPECT_EQ(getConnectionWrappers().size(), 0);

  // Verify that connection was pushed to established_connections_
  EXPECT_EQ(getEstablishedConnectionsSize(), 1);

  // Verify that trigger mechanism was executed.
  char trigger_byte;
  int pipe_read_fd = getTriggerPipeReadFd();
  EXPECT_GE(pipe_read_fd, 0);

  ssize_t bytes_read = ::read(pipe_read_fd, &trigger_byte, 1);
  EXPECT_EQ(bytes_read, 1) << "Expected to read 1 byte from trigger pipe, got " << bytes_read;
  EXPECT_EQ(trigger_byte, 1) << "Expected trigger byte to be 1, got "
                             << static_cast<int>(trigger_byte);

  // Step 3: Get the actual connection key that was used for tracking.
  // The connection key should be the local address of the connection.
  auto host_it = getHostToConnInfoMap().find("192.168.1.1");
  EXPECT_NE(host_it, getHostToConnInfoMap().end());

  // The connection key should have been added during onConnectionDone.
  // Let's find what connection key was actually used.
  std::string connection_key;
  if (!host_it->second.connection_keys.empty()) {
    connection_key = *host_it->second.connection_keys.begin();
    ENVOY_LOG_MISC(debug, "Found connection key: {}", connection_key);
  } else {
    // If no connection key was added, use a mock one for testing.
    connection_key = "192.168.1.1:12345";
    ENVOY_LOG_MISC(debug, "No connection key found, using mock: {}", connection_key);
  }

  // Step 4: Simulate downstream connection closure.
  io_handle_->onDownstreamConnectionClosed(connection_key);

  // Verify connection key is removed from host tracking.
  host_it = getHostToConnInfoMap().find("192.168.1.1");
  EXPECT_NE(host_it, getHostToConnInfoMap().end());
  EXPECT_EQ(host_it->second.connection_keys.count(connection_key), 0);

  // Step 5: Set up expectation for new connection attempts.
  auto mock_connection2 = setupMockConnection();

  Upstream::MockHost::MockCreateConnectionData success_conn_data2;
  success_conn_data2.connection_ = mock_connection2.get();
  success_conn_data2.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data2));

  mock_connection2.release();

  // Step 6: Trigger maintenance cycle to verify re-initiation.
  RemoteClusterConnectionConfig cluster_config("test-cluster", 1);

  maintainClusterConnections("test-cluster", cluster_config);

  // Since the connection key was removed, the host should need a new connection.
  // and initiateOneReverseConnection should be called again

  // Verify that a new wrapper was created.
  const auto& connection_wrappers2 = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers2.size(), 1);

  const auto& wrapper_to_host_map2 = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map2.size(), 1);

  RCConnectionWrapper* wrapper_ptr2 = connection_wrappers2[0].get();
  EXPECT_EQ(wrapper_to_host_map2.at(wrapper_ptr2), "192.168.1.1");

  // Verify stats show new connection attempt.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connecting"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.connecting"], 1);
}

TEST_F(ReverseConnectionIOHandleTest, SkipNewConnectionIfAttemptInProgress) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Create trigger pipe BEFORE initiating connection to ensure it's ready.
  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  // Set up priority set with hosts.
  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  // Create host map with a host.
  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);

  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).Times(0);

  // Create HostConnectionInfo entry.
  addHostConnectionInfo("192.168.1.1", "test-cluster", 1);

  // Simulate a upstream connection in connecting state.
  io_handle_->updateConnectionState("192.168.1.1", "test-cluster", "fake_pending_key",
                                    ReverseConnectionState::Connecting);

  RemoteClusterConnectionConfig cluster_config("test-cluster", 1);
  maintainClusterConnections("test-cluster", cluster_config);

  EXPECT_EQ(getConnectionWrappers().size(), 0);
}

// Test ReverseConnectionIOHandle::close() method without trigger pipe.
TEST_F(ReverseConnectionIOHandleTest, CloseMethodWithoutTriggerPipe) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Verify initial state - trigger pipe not ready.
  EXPECT_FALSE(isTriggerPipeReady());

  // Get initial file descriptor (this is the original socket FD)
  int initial_fd = io_handle_->fdDoNotUse();
  EXPECT_GE(initial_fd, 0);

  // Call close() - should close only the original socket FD and delegate to base class.
  auto result = io_handle_->close();

  // After close(), the FD should be -1.
  EXPECT_EQ(io_handle_->fdDoNotUse(), -1);
}

// Test ReverseConnectionIOHandle::close() method with trigger pipe.
TEST_F(ReverseConnectionIOHandleTest, CloseMethodWithTriggerPipe) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Get the original socket FD before creating trigger pipe.
  int original_socket_fd = io_handle_->fdDoNotUse();
  EXPECT_GE(original_socket_fd, 0);

  // Create trigger pipe and initialize file event to set up the scenario where fd_ points to.
  // trigger pipe Mock file event callback
  Event::FileReadyCb mock_callback = [](uint32_t) -> absl::Status { return absl::OkStatus(); };

  // Initialize file event to ensure the monitored FD is set to the trigger pipe.
  io_handle_->initializeFileEvent(dispatcher_, mock_callback, Event::FileTriggerType::Level,
                                  Event::FileReadyType::Read);
  EXPECT_TRUE(isTriggerPipeReady());

  // Get the pipe monitor FD (this becomes the monitored fd_ after initializeFileEvent)
  int pipe_monitor_fd = getTriggerPipeReadFd();
  EXPECT_GE(pipe_monitor_fd, 0);
  EXPECT_NE(original_socket_fd, pipe_monitor_fd); // Should be different FDs

  // Verify that the active FD is now the pipe monitor FD.
  EXPECT_EQ(io_handle_->fdDoNotUse(), pipe_monitor_fd);

  // Call close() - should:
  // 1. Close the original socket FD (original_socket_fd_)
  // 2. Let base class close() handle fd_

  auto result = io_handle_->close();
  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(io_handle_->fdDoNotUse(), -1);
}

// Test ReverseConnectionIOHandle::cleanup() method.
TEST_F(ReverseConnectionIOHandleTest, CleanupMethod) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Set up initial state with trigger pipe.
  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());
  EXPECT_GE(getTriggerPipeReadFd(), 0);
  EXPECT_GE(getTriggerPipeWriteFd(), 0);

  // Add some host connection info.
  addHostConnectionInfo("192.168.1.1", "test-cluster", 2);
  addHostConnectionInfo("192.168.1.2", "test-cluster", 1);

  // Verify initial state.
  EXPECT_EQ(getHostToConnInfoMap().size(), 2);
  EXPECT_TRUE(isTriggerPipeReady());

  // Call cleanup() - should reset all resources.
  cleanup();

  // Verify that trigger pipe FDs are reset to -1.
  EXPECT_FALSE(isTriggerPipeReady());
  EXPECT_EQ(getTriggerPipeReadFd(), -1);
  EXPECT_EQ(getTriggerPipeWriteFd(), -1);

  // Verify that host connection info is cleared.
  EXPECT_EQ(getHostToConnInfoMap().size(), 0);

  // Verify that connection wrappers are cleared.
  EXPECT_EQ(getConnectionWrappers().size(), 0);
  EXPECT_EQ(getConnWrapperToHostMap().size(), 0);

  // Verify that the base class fd_ is still valid (cleanup doesn't close the main socket)
  EXPECT_GE(io_handle_->fdDoNotUse(), 0);
}

// Test cleanup() closes any established connections in the queue.
TEST_F(ReverseConnectionIOHandleTest, CleanupClosesEstablishedConnections) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Create two mock connections and add them to the established queue.
  // 1) An open connection should be closed with FlushWrite.
  {
    auto open_conn = std::make_unique<NiceMock<Network::MockClientConnection>>();
    EXPECT_CALL(*open_conn, state()).WillOnce(Return(Network::Connection::State::Open));
    EXPECT_CALL(*open_conn, close(Network::ConnectionCloseType::FlushWrite));
    addConnectionToEstablishedQueue(std::move(open_conn));
  }
  // 2) A closed connection should not be closed again.
  {
    auto closed_conn = std::make_unique<NiceMock<Network::MockClientConnection>>();
    EXPECT_CALL(*closed_conn, state()).WillOnce(Return(Network::Connection::State::Closed));
    // No close() expected for closed connection.
    addConnectionToEstablishedQueue(std::move(closed_conn));
  }

  // Call cleanup and ensure queue is drained without crashes.
  EXPECT_GT(getEstablishedConnectionsSize(), 0);
  cleanup();
  EXPECT_EQ(getEstablishedConnectionsSize(), 0);
}

// Test initializeFileEvent early-return path when already started.
TEST_F(ReverseConnectionIOHandleTest, InitializeFileEventSkipWhenAlreadyStarted) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  Event::FileReadyCb cb = [](uint32_t) -> absl::Status { return absl::OkStatus(); };
  io_handle_->initializeFileEvent(dispatcher_, cb, Event::FileTriggerType::Level, 0);

  // Call again; should skip without changing fd or creating a new pipe.
  const int fd_before = io_handle_->fdDoNotUse();
  io_handle_->initializeFileEvent(dispatcher_, cb, Event::FileTriggerType::Level, 0);
  EXPECT_EQ(fd_before, io_handle_->fdDoNotUse());
}

// Test maintainReverseConnections early return when src_node_id is empty.
TEST_F(ReverseConnectionIOHandleTest, MaintainReverseConnectionsMissingSrcNodeId) {
  ReverseConnectionSocketConfig cfg;
  cfg.src_cluster_id = "test-cluster";
  cfg.src_node_id = ""; // Intentionally empty
  cfg.remote_clusters.push_back(RemoteClusterConnectionConfig("remote", 1));

  io_handle_ = createTestIOHandle(cfg);
  EXPECT_NE(io_handle_, nullptr);
  maintainReverseConnections();
}

// Test maintainClusterConnections early return when cluster is not found.
TEST_F(ReverseConnectionIOHandleTest, MaintainClusterConnectionsNoCluster) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  RemoteClusterConnectionConfig cluster_cfg{"missing-cluster", 1};
  // Default mock ClusterManager returns nullptr for unknown cluster.
  maintainClusterConnections("missing-cluster", cluster_cfg);
}

// Test shouldAttemptConnectionToHost creates host entry on-demand and returns true.
TEST_F(ReverseConnectionIOHandleTest, ShouldAttemptConnectionCreatesHostEntry) {
  // Set up TLS registry to provide a time source for getTimeSource().
  setupThreadLocalSlot();
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  EXPECT_TRUE(shouldAttemptConnectionToHost("10.0.0.5", "cluster-x"));
  const auto& map = getHostToConnInfoMap();
  EXPECT_NE(map.find("10.0.0.5"), map.end());
}

// Test maybeUpdateHostsMappingsAndConnections removes stale hosts.
TEST_F(ReverseConnectionIOHandleTest, MaybeUpdateHostsRemovesStaleHosts) {
  // Ensure a valid time source via TLS registry.
  setupThreadLocalSlot();
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Initial set of hosts: a, b
  maybeUpdateHostsMappingsAndConnections("c1", std::vector<std::string>{"a", "b"});
  EXPECT_EQ(getHostToConnInfoMap().size(), 2);

  // Updated set: only a → b should be removed.
  maybeUpdateHostsMappingsAndConnections("c1", std::vector<std::string>{"a"});
  const auto& map = getHostToConnInfoMap();
  EXPECT_NE(map.find("a"), map.end());
  EXPECT_EQ(map.find("b"), map.end());
}

// Lightly exercise read/write/connect wrappers for coverage.
TEST_F(ReverseConnectionIOHandleTest, ReadWriteConnectCoverage) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  Buffer::OwnedImpl buf("hello");
  (void)io_handle_->write(buf);
  Buffer::OwnedImpl rbuf;
  (void)io_handle_->read(rbuf, absl::optional<uint64_t>(64));

  auto addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  (void)io_handle_->connect(addr);
}

// Test ReverseConnectionIOHandle::onAboveWriteBufferHighWatermark method (no-op)
TEST_F(ReverseConnectionIOHandleTest, OnAboveWriteBufferHighWatermark) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Call onAboveWriteBufferHighWatermark - should be a no-op.
  io_handle_->onAboveWriteBufferHighWatermark();
  // The test passes if no exceptions are thrown.
}

// Test ReverseConnectionIOHandle::onBelowWriteBufferLowWatermark method (no-op)
TEST_F(ReverseConnectionIOHandleTest, OnBelowWriteBufferLowWatermark) {
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Call onBelowWriteBufferLowWatermark - should be a no-op.
  io_handle_->onBelowWriteBufferLowWatermark();
  // The test passes if no exceptions are thrown.
}

// Test updateStateGauge() method with null extension.
TEST_F(ReverseConnectionIOHandleTest, UpdateStateGaugeWithNullExtension) {
  // Create a test IO handle with null extension BEFORE setting up thread local slot.
  auto config = createDefaultTestConfig();
  int test_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  EXPECT_GE(test_fd, 0);

  auto io_handle_null_extension = std::make_unique<ReverseConnectionIOHandle>(
      test_fd, config, cluster_manager_, nullptr, *stats_scope_);

  // Call updateConnectionState which internally calls updateStateGauge.
  // This should exit early when extension is null.
  io_handle_null_extension->updateConnectionState("test-host2", "test-cluster", "test-key2",
                                                  ReverseConnectionState::Connected);

  // Now set up thread local slot and create a test IO handle with extension.
  setupThreadLocalSlot();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  io_handle_->updateConnectionState("test-host", "test-cluster", "test-key",
                                    ReverseConnectionState::Connected);

  // Verify that stats were updated with extension.
  auto stat_map_with_extension = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map_with_extension["test_scope.reverse_connections.host.test-host.connected"], 1);
  EXPECT_EQ(
      stat_map_with_extension["test_scope.reverse_connections.cluster.test-cluster.connected"], 1);

  // Check that no stats exist for the null extension call
  EXPECT_EQ(stat_map_with_extension["test_scope.reverse_connections.host.test-host2.connected"], 0);
}

// Test updateStateGauge() method with unknown state.
TEST_F(ReverseConnectionIOHandleTest, UpdateStateGaugeWithUnknownState) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

  // Create a test IO handle with extension.
  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // First ensure host entry exists so the updateConnectionState call doesn't fail.
  addHostConnectionInfo("test-host", "test-cluster", 1);

  // Call updateConnectionState with an unknown state value.
  // We'll use a value that's not in the enum to trigger the default case.
  io_handle_->updateConnectionState("test-host", "test-cluster", "test-key",
                                    static_cast<ReverseConnectionState>(999));

  // Verify that the unknown state was handled correctly by checking if a gauge was created
  // with "unknown" suffix.
  auto stat_map = extension_->getCrossWorkerStatMap();

  // The unknown state should have been handled and a gauge with "unknown" suffix should exist.
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.test-host.unknown"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.unknown"], 1);
}

// Test ReverseConnectionIOHandle::accept() method - trigger pipe edge cases.
TEST_F(ReverseConnectionIOHandleTest, AcceptMethodTriggerPipeEdgeCases) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  // Test Case 1: Trigger pipe not ready - should return nullptr.
  auto result = io_handle_->accept(nullptr, nullptr);
  EXPECT_EQ(result, nullptr);

  // Create trigger pipe.
  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Test Case 2: Trigger pipe ready but no data to read (EAGAIN/EWOULDBLOCK) - should return
  // nullptr.
  result = io_handle_->accept(nullptr, nullptr);
  EXPECT_EQ(result, nullptr);

  // Test Case 3: Trigger pipe closed (read returns 0) - should return nullptr.
  ::close(getTriggerPipeWriteFd());
  result = io_handle_->accept(nullptr, nullptr);
  EXPECT_EQ(result, nullptr);
  createTriggerPipe();

  // Test Case 4: Trigger pipe read error (not EAGAIN/EWOULDBLOCK) - should return nullptr.
  ::close(getTriggerPipeReadFd());
  result = io_handle_->accept(nullptr, nullptr);
  EXPECT_EQ(result, nullptr);
  createTriggerPipe();

  // Test Case 5: Trigger pipe ready, data read, but no established connections - should return
  // nullptr.
  char trigger_byte = 1;
  ssize_t bytes_written = ::write(getTriggerPipeWriteFd(), &trigger_byte, 1);
  EXPECT_EQ(bytes_written, 1);

  result = io_handle_->accept(nullptr, nullptr);
  EXPECT_EQ(result, nullptr);
}

// Test ReverseConnectionIOHandle::accept() method - successful accept with address parameters.
TEST_F(ReverseConnectionIOHandleTest, AcceptMethodSuccessfulWithAddress) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();

  // Set up connection info provider with remote address.
  auto mock_remote_address =
      std::make_shared<Network::Address::Ipv4Instance>("192.168.1.100", 8080);
  auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12345);

  EXPECT_CALL(*mock_connection, connectionInfoProvider())
      .WillRepeatedly(Invoke(
          [mock_remote_address, mock_local_address]() -> const Network::ConnectionInfoProvider& {
            static auto mock_provider = std::make_unique<Network::ConnectionInfoSetterImpl>(
                mock_local_address, mock_remote_address);
            return *mock_provider;
          }));

  // Set up socket expectations.
  EXPECT_CALL(*mock_connection, close(Network::ConnectionCloseType::NoFlush));

  // Add connection to the established queue.
  addConnectionToEstablishedQueue(std::move(mock_connection));

  // Write trigger byte.
  char trigger_byte = 1;
  ssize_t bytes_written = ::write(getTriggerPipeWriteFd(), &trigger_byte, 1);
  EXPECT_EQ(bytes_written, 1);

  // Test accept with address parameters.
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addr);
  auto result = io_handle_->accept(reinterpret_cast<struct sockaddr*>(&addr), &addrlen);

  EXPECT_NE(result, nullptr);
  EXPECT_EQ(addrlen, sizeof(addr));
  EXPECT_EQ(addr.sin_family, AF_INET);
}

// Test ReverseConnectionIOHandle::accept() method - address handling edge cases.
TEST_F(ReverseConnectionIOHandleTest, AcceptMethodAddressHandlingEdgeCases) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Test Case 1: Address buffer too small for remote address.
  {
    auto mock_connection = setupMockConnection();

    auto mock_remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("192.168.1.101", 8080);
    auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12346);

    EXPECT_CALL(*mock_connection, connectionInfoProvider())
        .WillRepeatedly(Invoke(
            [mock_remote_address, mock_local_address]() -> const Network::ConnectionInfoProvider& {
              static auto mock_provider = std::make_unique<Network::ConnectionInfoSetterImpl>(
                  mock_local_address, mock_remote_address);
              return *mock_provider;
            }));

    EXPECT_CALL(*mock_connection, close(Network::ConnectionCloseType::NoFlush));

    addConnectionToEstablishedQueue(std::move(mock_connection));

    char trigger_byte = 1;
    ssize_t bytes_written = ::write(getTriggerPipeWriteFd(), &trigger_byte, 1);
    EXPECT_EQ(bytes_written, 1);

    struct sockaddr_in addr;
    socklen_t addrlen = 1; // Too small
    auto result = io_handle_->accept(reinterpret_cast<struct sockaddr*>(&addr), &addrlen);

    EXPECT_NE(result, nullptr);
    EXPECT_GT(addrlen, 1);
  }

  // Test Case 2: No remote address, fallback to synthetic address.
  {
    auto mock_connection = setupMockConnection();

    auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12347);

    EXPECT_CALL(*mock_connection, connectionInfoProvider())
        .WillRepeatedly(Invoke([mock_local_address]() -> const Network::ConnectionInfoProvider& {
          static auto mock_provider =
              std::make_unique<Network::ConnectionInfoSetterImpl>(mock_local_address, nullptr);
          return *mock_provider;
        }));

    EXPECT_CALL(*mock_connection, close(Network::ConnectionCloseType::NoFlush));

    addConnectionToEstablishedQueue(std::move(mock_connection));

    char trigger_byte = 1;
    ssize_t bytes_written = ::write(getTriggerPipeWriteFd(), &trigger_byte, 1);
    EXPECT_EQ(bytes_written, 1);

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    auto result = io_handle_->accept(reinterpret_cast<struct sockaddr*>(&addr), &addrlen);

    EXPECT_NE(result, nullptr);
    EXPECT_EQ(addrlen, sizeof(addr));
    EXPECT_EQ(addr.sin_family, AF_INET);
    EXPECT_EQ(addr.sin_addr.s_addr, htonl(INADDR_LOOPBACK));
  }

  // Test Case 3: Synthetic address buffer too small.
  {
    auto mock_connection = setupMockConnection();

    auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12348);

    EXPECT_CALL(*mock_connection, connectionInfoProvider())
        .WillRepeatedly(Invoke([mock_local_address]() -> const Network::ConnectionInfoProvider& {
          static auto mock_provider =
              std::make_unique<Network::ConnectionInfoSetterImpl>(mock_local_address, nullptr);
          return *mock_provider;
        }));

    EXPECT_CALL(*mock_connection, close(Network::ConnectionCloseType::NoFlush));

    addConnectionToEstablishedQueue(std::move(mock_connection));

    char trigger_byte = 1;
    ssize_t bytes_written = ::write(getTriggerPipeWriteFd(), &trigger_byte, 1);
    EXPECT_EQ(bytes_written, 1);

    struct sockaddr_in addr;
    socklen_t addrlen = 1; // Too small
    auto result = io_handle_->accept(reinterpret_cast<struct sockaddr*>(&addr), &addrlen);

    EXPECT_NE(result, nullptr);
    EXPECT_GT(addrlen, 1);
  }
}

// Test ReverseConnectionIOHandle::accept() method - successful accept scenarios.
TEST_F(ReverseConnectionIOHandleTest, AcceptMethodSuccessfulScenarios) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Test Case 1: Accept without address parameters.
  {
    auto mock_connection = setupMockConnection();

    auto mock_remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("192.168.1.102", 8080);
    auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12349);

    EXPECT_CALL(*mock_connection, connectionInfoProvider())
        .WillRepeatedly(Invoke(
            [mock_remote_address, mock_local_address]() -> const Network::ConnectionInfoProvider& {
              static auto mock_provider = std::make_unique<Network::ConnectionInfoSetterImpl>(
                  mock_local_address, mock_remote_address);
              return *mock_provider;
            }));

    EXPECT_CALL(*mock_connection, close(Network::ConnectionCloseType::NoFlush));

    addConnectionToEstablishedQueue(std::move(mock_connection));

    char trigger_byte = 1;
    ssize_t bytes_written = ::write(getTriggerPipeWriteFd(), &trigger_byte, 1);
    EXPECT_EQ(bytes_written, 1);

    auto result = io_handle_->accept(nullptr, nullptr);
    EXPECT_NE(result, nullptr);
  }
}

// Test ReverseConnectionIOHandle::accept() method - socket and file descriptor failures.
TEST_F(ReverseConnectionIOHandleTest, AcceptMethodSocketAndFdFailures) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Test Case 1: Original socket not available or not open.
  {
    auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();

    // Create a mock socket that returns isOpen() = false.
    auto mock_socket_ptr = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();

    // Set up IO handle expectations.
    EXPECT_CALL(*mock_io_handle, resetFileEvents()).WillRepeatedly(Return());
    EXPECT_CALL(*mock_io_handle, isOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_io_handle, duplicate()).WillRepeatedly(Invoke([]() {
      auto duplicated_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
      EXPECT_CALL(*duplicated_handle, isOpen()).WillRepeatedly(Return(true));
      return duplicated_handle;
    }));

    // Set up socket expectations - but isOpen returns false to simulate failure.
    EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));
    EXPECT_CALL(*mock_socket_ptr, isOpen()).WillRepeatedly(Return(false));

    // Store the mock_io_handle in the socket before casting.
    mock_socket_ptr->io_handle_ = std::move(mock_io_handle);

    // Create the socket and set up connection expectations.
    auto mock_socket = std::unique_ptr<Network::ConnectionSocket>(mock_socket_ptr.release());
    EXPECT_CALL(*mock_connection, getSocket()).WillRepeatedly(ReturnRef(mock_socket));

    auto mock_remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("192.168.1.103", 8080);
    auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12350);

    EXPECT_CALL(*mock_connection, connectionInfoProvider())
        .WillRepeatedly(Invoke(
            [mock_remote_address, mock_local_address]() -> const Network::ConnectionInfoProvider& {
              static auto mock_provider = std::make_unique<Network::ConnectionInfoSetterImpl>(
                  mock_local_address, mock_remote_address);
              return *mock_provider;
            }));

    addConnectionToEstablishedQueue(std::move(mock_connection));

    char trigger_byte = 1;
    ssize_t bytes_written = ::write(getTriggerPipeWriteFd(), &trigger_byte, 1);
    EXPECT_EQ(bytes_written, 1);

    auto result = io_handle_->accept(nullptr, nullptr);
    EXPECT_EQ(result, nullptr);
  }

  // Test Case 2: Failed to duplicate file descriptor.
  {
    auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();

    // Create a mock socket with IO handle that fails to duplicate.
    auto mock_socket_ptr = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();

    // Set up IO handle expectations - but duplicate returns nullptr to simulate failure.
    EXPECT_CALL(*mock_io_handle, resetFileEvents()).WillRepeatedly(Return());
    EXPECT_CALL(*mock_io_handle, isOpen()).WillRepeatedly(Return(true));
    EXPECT_CALL(*mock_io_handle, duplicate()).WillRepeatedly(Invoke([]() {
      return std::unique_ptr<Network::IoHandle>(nullptr);
    }));

    // Set up socket expectations.
    EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));
    EXPECT_CALL(*mock_socket_ptr, isOpen()).WillRepeatedly(Return(true));

    // Store the mock_io_handle in the socket before casting.
    mock_socket_ptr->io_handle_ = std::move(mock_io_handle);

    // Create the socket and set up connection expectations.
    auto mock_socket = std::unique_ptr<Network::ConnectionSocket>(mock_socket_ptr.release());
    EXPECT_CALL(*mock_connection, getSocket()).WillRepeatedly(ReturnRef(mock_socket));

    auto mock_remote_address =
        std::make_shared<Network::Address::Ipv4Instance>("192.168.1.104", 8080);
    auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12351);

    EXPECT_CALL(*mock_connection, connectionInfoProvider())
        .WillRepeatedly(Invoke(
            [mock_remote_address, mock_local_address]() -> const Network::ConnectionInfoProvider& {
              static auto mock_provider = std::make_unique<Network::ConnectionInfoSetterImpl>(
                  mock_local_address, mock_remote_address);
              return *mock_provider;
            }));

    addConnectionToEstablishedQueue(std::move(mock_connection));

    char trigger_byte = 1;
    ssize_t bytes_written = ::write(getTriggerPipeWriteFd(), &trigger_byte, 1);
    EXPECT_EQ(bytes_written, 1);

    auto result = io_handle_->accept(nullptr, nullptr);
    EXPECT_EQ(result, nullptr);
  }
}

// Tests the case where dynamic_cast succeeds and SSL_set_quiet_shutdown is called.
TEST_F(ReverseConnectionIOHandleTest, OnConnectionDoneTlsConnectionQuietShutdownSuccess) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);
  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  addHostConnectionInfo("192.168.1.1", "test-cluster", 1);

  // Create a mock connection with TLS.
  auto mock_connection = setupMockConnection();

  // Create a mock SSL object to verify SSL_set_quiet_shutdown is called.
  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  ASSERT_NE(ctx.get(), nullptr);
  SSL* mock_ssl = SSL_new(ctx.get());
  ASSERT_NE(mock_ssl, nullptr);

  // Create MockSslHandshakerImpl that extends the real SslHandshakerImpl.
  // This will make the dynamic_cast succeed.
  // Pass the SSL object to the constructor so it doesn't crash.
  auto mock_ssl_handshaker = std::make_shared<MockSslHandshakerImpl>(mock_ssl);

  // Mock ssl() to return MockSslHandshakerImpl.
  EXPECT_CALL(*mock_connection, ssl()).WillRepeatedly(Return(mock_ssl_handshaker));

  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Initiate connection to create wrapper.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();

  // Before calling onConnectionDone, verify quiet_shutdown is not set (default is 0).
  EXPECT_EQ(SSL_get_quiet_shutdown(mock_ssl), 0);

  // Call onConnectionDone with success - this should trigger the SSL quiet shutdown code path.
  // The dynamic_cast will succeed, and SSL_set_quiet_shutdown(ssl, 1) will be called.
  io_handle_->onConnectionDone("reverse connection accepted", wrapper_ptr, false);

  // Verify SSL_set_quiet_shutdown was called by checking the SSL object's quiet_shutdown flag.
  EXPECT_EQ(SSL_get_quiet_shutdown(mock_ssl), 1);

  // Verify wrapper was cleaned up.
  EXPECT_EQ(getConnWrapperToHostMap().size(), 0);
  EXPECT_EQ(getConnectionWrappers().size(), 0);

  // Verify connection was queued.
  EXPECT_EQ(getEstablishedConnectionsSize(), 1);

  // Verify stats show connected state.
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connected"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.connected"], 1);
}

// Test onConnectionDone with TLS connection where dynamic_cast fails (mock doesn't derive from
// SslHandshakerImpl).
TEST_F(ReverseConnectionIOHandleTest, OnConnectionDoneTlsConnectionDynamicCastFails) {
  setupThreadLocalSlot();

  auto config = createDefaultTestConfig();
  io_handle_ = createTestIOHandle(config);
  EXPECT_NE(io_handle_, nullptr);

  createTriggerPipe();
  EXPECT_TRUE(isTriggerPipeReady());

  // Set up mock thread local cluster.
  auto mock_thread_local_cluster = std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster("test-cluster"))
      .WillRepeatedly(Return(mock_thread_local_cluster.get()));

  auto mock_priority_set = std::make_shared<NiceMock<Upstream::MockPrioritySet>>();
  EXPECT_CALL(*mock_thread_local_cluster, prioritySet())
      .WillRepeatedly(ReturnRef(*mock_priority_set));

  auto host_map = std::make_shared<Upstream::HostMap>();
  auto mock_host = createMockHost("192.168.1.1");
  (*host_map)["192.168.1.1"] = std::const_pointer_cast<Upstream::Host>(mock_host);
  EXPECT_CALL(*mock_priority_set, crossPriorityHostMap()).WillRepeatedly(Return(host_map));

  addHostConnectionInfo("192.168.1.1", "test-cluster", 1);

  // Create a mock connection WITH SSL (TLS connection).
  auto mock_connection = setupMockConnection();

  // Create a mock SSL connection info that does not derive from SslHandshakerImpl.
  // This will cause the dynamic_cast to fail.
  auto mock_ssl_info = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();

  // Mock ssl() to return non-null, indicating TLS connection.
  EXPECT_CALL(*mock_connection, ssl()).WillRepeatedly(Return(mock_ssl_info));

  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Initiate connection to create wrapper.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();

  // Call onConnectionDone with success, this should trigger the SSL quiet shutdown code path.
  // Since we're using a mock SSL connection info that doesn't derive from SslHandshakerImpl,
  // the dynamic_cast will fail.
  io_handle_->onConnectionDone("reverse connection accepted", wrapper_ptr, false);

  // Verify wrapper was cleaned up.
  EXPECT_EQ(getConnWrapperToHostMap().size(), 0);
  EXPECT_EQ(getConnectionWrappers().size(), 0);

  EXPECT_EQ(getEstablishedConnectionsSize(), 1);
  auto stat_map = extension_->getCrossWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.host.192.168.1.1.connected"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.cluster.test-cluster.connected"], 1);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
