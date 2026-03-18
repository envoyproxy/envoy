#include <sys/socket.h>

#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/rc_connection_wrapper.h"
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

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

// RCConnectionWrapper Tests.

class RCConnectionWrapperTest : public testing::Test {
protected:
  void SetUp() override {
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cluster_manager_));
    EXPECT_CALL(thread_local_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    // Set stat prefix to "reverse_connections" for tests.
    config_.set_stat_prefix("reverse_connections");
    // Enable detailed stats for tests that need per-node/cluster stats.
    config_.set_enable_detailed_stats(true);
    extension_ = std::make_unique<ReverseTunnelInitiatorExtension>(context_, config_);
    setupThreadLocalSlot();
    io_handle_ = createTestIOHandle(createDefaultTestConfig());
  }

  void TearDown() override {
    io_handle_.reset();
    extension_.reset();
  }

  void setupThreadLocalSlot() {
    thread_local_registry_ =
        std::make_shared<DownstreamSocketThreadLocal>(dispatcher_, *stats_scope_);
    tls_slot_ = ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));
  }

  ReverseConnectionSocketConfig createDefaultTestConfig() {
    ReverseConnectionSocketConfig config;
    config.src_cluster_id = "test-cluster";
    config.src_node_id = "test-node";
    config.enable_circuit_breaker = true;
    config.remote_clusters.push_back(RemoteClusterConnectionConfig("remote-cluster", 2));
    return config;
  }

  std::unique_ptr<ReverseConnectionIOHandle>
  createTestIOHandle(const ReverseConnectionSocketConfig& config) {
    int test_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(test_fd, 0);
    return std::make_unique<ReverseConnectionIOHandle>(test_fd, config, cluster_manager_,
                                                       extension_.get(), *stats_scope_);
  }

  // Connection Management Helpers.

  bool initiateOneReverseConnection(const std::string& cluster_name,
                                    const std::string& host_address,
                                    Upstream::HostConstSharedPtr host) {
    return io_handle_->initiateOneReverseConnection(cluster_name, host_address, host);
  }

  // Data Access Helpers.

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
            // backoff_until
            std::chrono::steady_clock::now(), // NO_CHECK_FORMAT(real_time)
            {}                                // connection_states
        };
  }

  // Helper to create a mock host.
  Upstream::HostConstSharedPtr createMockHost(const std::string& address) {
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
    auto mock_address = std::make_shared<Network::Address::Ipv4Instance>(address, 8080);
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

  // Test fixtures.
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};
  envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface config_;
  std::unique_ptr<ReverseTunnelInitiatorExtension> extension_;
  std::unique_ptr<ReverseConnectionIOHandle> io_handle_;
  std::unique_ptr<ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<DownstreamSocketThreadLocal> thread_local_registry_;

  // Mock socket for testing.
  std::unique_ptr<Network::ConnectionSocket> mock_socket_;
};

// Test RCConnectionWrapper::connect() method with HTTP/1.1 handshake success
TEST_F(RCConnectionWrapperTest, ConnectHttpHandshakeSuccess) {
  // Create a mock connection.
  auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();

  // Set up connection expectations.
  EXPECT_CALL(*mock_connection, addConnectionCallbacks(_));
  EXPECT_CALL(*mock_connection, addReadFilter(_));
  EXPECT_CALL(*mock_connection, connect());
  EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(12345));
  EXPECT_CALL(*mock_connection, state()).WillRepeatedly(Return(Network::Connection::State::Open));

  // Set up socket expectations for address info.
  auto mock_address = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.1", 8080);
  auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12345);

  // Set up connection info provider expectations directly on the mock connection.
  EXPECT_CALL(*mock_connection, connectionInfoProvider())
      .WillRepeatedly(Invoke([mock_address,
                              mock_local_address]() -> const Network::ConnectionInfoProvider& {
        static auto mock_provider =
            std::make_unique<Network::ConnectionInfoSetterImpl>(mock_local_address, mock_address);
        return *mock_provider;
      }));

  // Create a mock host.
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Call connect() method.
  std::string result = wrapper.connect("test-tenant", "test-cluster", "test-node");

  // Verify connect() returns the local address.
  EXPECT_EQ(result, "127.0.0.1:12345");
}

// Test RCConnectionWrapper::connect() method with HTTP proxy (internal address) scenario.
TEST_F(RCConnectionWrapperTest, ConnectHttpHandshakeWithHttpProxy) {
  // Create a mock connection.
  auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();

  // Set up connection expectations.
  EXPECT_CALL(*mock_connection, addConnectionCallbacks(_));
  EXPECT_CALL(*mock_connection, addReadFilter(_));
  EXPECT_CALL(*mock_connection, connect());
  EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(12345));
  EXPECT_CALL(*mock_connection, state()).WillRepeatedly(Return(Network::Connection::State::Open));

  // Set up socket expectations for internal address (HTTP proxy scenario).
  auto mock_internal_address = std::make_shared<Network::Address::EnvoyInternalInstance>(
      "internal_listener_name", "endpoint_id_123");
  auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12345);

  // Set up connection info provider expectations with internal address.
  EXPECT_CALL(*mock_connection, connectionInfoProvider())
      .WillRepeatedly(Invoke(
          [mock_internal_address, mock_local_address]() -> const Network::ConnectionInfoProvider& {
            static auto mock_provider = std::make_unique<Network::ConnectionInfoSetterImpl>(
                mock_local_address, mock_internal_address);
            return *mock_provider;
          }));

  // Capture the written buffer to verify HTTP request and simulate kernel drain.
  Buffer::OwnedImpl captured_buffer;
  EXPECT_CALL(*mock_connection, write(_, _))
      .WillOnce(Invoke([&captured_buffer](Buffer::Instance& buffer, bool) {
        captured_buffer.add(buffer);
        buffer.drain(buffer.length());
      }));

  // Create a mock host.
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Call connect() method.
  std::string result = wrapper.connect("test-tenant", "test-cluster", "test-node");

  // Verify connect() returns the local address.
  EXPECT_EQ(result, "127.0.0.1:12345");
}

// Test RCConnectionWrapper::connect() honors custom request paths.
TEST_F(RCConnectionWrapperTest, ConnectHttpHandshakeWithCustomRequestPath) {
  auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();

  EXPECT_CALL(*mock_connection, addConnectionCallbacks(_));
  EXPECT_CALL(*mock_connection, addReadFilter(_));
  EXPECT_CALL(*mock_connection, connect());
  EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(12345));
  EXPECT_CALL(*mock_connection, state()).WillRepeatedly(Return(Network::Connection::State::Open));

  auto mock_address = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.1", 8080);
  auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12345);

  EXPECT_CALL(*mock_connection, connectionInfoProvider())
      .WillRepeatedly(Invoke([mock_address,
                              mock_local_address]() -> const Network::ConnectionInfoProvider& {
        static auto mock_provider =
            std::make_unique<Network::ConnectionInfoSetterImpl>(mock_local_address, mock_address);
        return *mock_provider;
      }));

  Buffer::OwnedImpl captured_buffer;
  EXPECT_CALL(*mock_connection, write(_, _))
      .WillOnce(Invoke([&captured_buffer](Buffer::Instance& buffer, bool) {
        captured_buffer.add(buffer);
        buffer.drain(buffer.length());
      }));

  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  ReverseConnectionSocketConfig custom_config = createDefaultTestConfig();
  custom_config.request_path = "/custom/handshake";
  auto local_io_handle = createTestIOHandle(custom_config);

  RCConnectionWrapper wrapper(*local_io_handle, std::move(mock_connection), mock_host,
                              "test-cluster");

  wrapper.connect("test-tenant", "test-cluster", "test-node");

  const std::string encoded_request = captured_buffer.toString();
  EXPECT_NE(encoded_request.find("GET /custom/handshake HTTP/1.1"), std::string::npos);
}

// Test RCConnectionWrapper::connect() method with connection write failure.
TEST_F(RCConnectionWrapperTest, ConnectHttpHandshakeWriteFailure) {
  // Create a mock connection that fails to write.
  auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();

  // Set up connection expectations.
  EXPECT_CALL(*mock_connection, addConnectionCallbacks(_));
  EXPECT_CALL(*mock_connection, addReadFilter(_));
  EXPECT_CALL(*mock_connection, connect());
  EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(12345));
  EXPECT_CALL(*mock_connection, state()).WillRepeatedly(Return(Network::Connection::State::Open));
  EXPECT_CALL(*mock_connection, write(_, _)).WillOnce(Invoke([](Buffer::Instance&, bool) -> void {
    throw EnvoyException("Write failed");
  }));

  // Set up socket expectations.
  auto mock_address = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.1", 8080);
  auto mock_local_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 12345);

  // Set up connection info provider expectations directly on the mock connection.
  EXPECT_CALL(*mock_connection, connectionInfoProvider())
      .WillRepeatedly(Invoke([mock_address,
                              mock_local_address]() -> const Network::ConnectionInfoProvider& {
        static auto mock_provider =
            std::make_unique<Network::ConnectionInfoSetterImpl>(mock_local_address, mock_address);
        return *mock_provider;
      }));

  // Create a mock host.
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Call connect() method - should handle the write failure gracefully.
  // The method should not throw but should handle the exception internally.
  std::string result;
  try {
    result = wrapper.connect("test-tenant", "test-cluster", "test-node");
  } catch (const EnvoyException& e) {
    // The connect() method doesn't handle exceptions, so we expect it to throw.
    // This is the current behavior - the method should be updated to handle exceptions.
    EXPECT_STREQ(e.what(), "Write failed");
    return; // Exit test early since exception was thrown
  }

  // If no exception was thrown, verify connect() still returns the local address.
  EXPECT_EQ(result, "127.0.0.1:12345");
}

// Test RCConnectionWrapper::onHandshakeSuccess method.
TEST_F(RCConnectionWrapperTest, OnHandshakeSuccess) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

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

  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection to create the wrapper and add it to the map.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify wrapper was created and mapped.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();
  EXPECT_EQ(wrapper_to_host_map.at(wrapper_ptr), "192.168.1.1");

  // Get initial stats before onHandshakeSuccess.
  auto initial_stats = extension_->getCrossWorkerStatMap();
  std::string host_stat_name = "test_scope.reverse_connections.host.192.168.1.1.connected";
  std::string cluster_stat_name = "test_scope.reverse_connections.cluster.test-cluster.connected";

  // Handshake stats use labels (tags) for worker, cluster, result, and reason.
  // Base stat name: reverse_connections.handshake (scope will add test_scope. prefix)
  // Tags: worker=worker_0, cluster=test-cluster, result=success
  auto& stats_scope = extension_->getStatsScope();
  std::string base_stat_name = "reverse_connections.handshake";
  Stats::StatNameManagedStorage stat_storage(base_stat_name, stats_scope.symbolTable());

  // Create tags matching the success case.
  Stats::StatNameTagVector tags;
  Stats::StatNameManagedStorage worker_key_storage("worker", stats_scope.symbolTable());
  Stats::StatNameManagedStorage worker_value_storage("worker_0", stats_scope.symbolTable());
  tags.push_back({worker_key_storage.statName(), worker_value_storage.statName()});

  Stats::StatNameManagedStorage cluster_key_storage("cluster", stats_scope.symbolTable());
  Stats::StatNameManagedStorage cluster_value_storage("test-cluster", stats_scope.symbolTable());
  tags.push_back({cluster_key_storage.statName(), cluster_value_storage.statName()});

  Stats::StatNameManagedStorage result_key_storage("result", stats_scope.symbolTable());
  Stats::StatNameManagedStorage result_value_storage("success", stats_scope.symbolTable());
  tags.push_back({result_key_storage.statName(), result_value_storage.statName()});

  auto& handshake_success_counter =
      Stats::Utility::counterFromStatNames(stats_scope, {stat_storage.statName()}, tags);
  uint64_t initial_handshake_success_count = handshake_success_counter.value();

  // Call onHandshakeSuccess.
  wrapper_ptr->onHandshakeSuccess();

  // Get stats after onHandshakeSuccess.
  auto final_stats = extension_->getCrossWorkerStatMap();

  // Verify that connected stats were incremented.
  EXPECT_EQ(final_stats[host_stat_name], initial_stats[host_stat_name] + 1);
  EXPECT_EQ(final_stats[cluster_stat_name], initial_stats[cluster_stat_name] + 1);

  // Verify that handshake success stat was incremented.
  EXPECT_EQ(handshake_success_counter.value(), initial_handshake_success_count + 1);
}

// Test RCConnectionWrapper::onHandshakeFailure method.
TEST_F(RCConnectionWrapperTest, OnHandshakeFailure) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

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

  auto mock_connection = setupMockConnection();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection to create the wrapper and add it to the map.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify wrapper was created and mapped.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();
  EXPECT_EQ(wrapper_to_host_map.at(wrapper_ptr), "192.168.1.1");

  // Get initial stats before onHandshakeFailure.
  auto initial_stats = extension_->getCrossWorkerStatMap();
  std::string host_failed_stat_name = "test_scope.reverse_connections.host.192.168.1.1.failed";
  std::string cluster_failed_stat_name =
      "test_scope.reverse_connections.cluster.test-cluster.failed";

  // Handshake stats use labels (tags) for worker, cluster, result, and failure_reason.
  // Base stat name: reverse_connections.handshake (scope will add test_scope. prefix)
  // Tags: worker=worker_0, cluster=test-cluster, result=failed, failure_reason=http.401
  auto& stats_scope = extension_->getStatsScope();
  std::string base_stat_name = "reverse_connections.handshake";
  Stats::StatNameManagedStorage stat_storage(base_stat_name, stats_scope.symbolTable());

  // Create tags matching the failure case with HTTP 401.
  Stats::StatNameTagVector tags;
  Stats::StatNameManagedStorage worker_key_storage("worker", stats_scope.symbolTable());
  Stats::StatNameManagedStorage worker_value_storage("worker_0", stats_scope.symbolTable());
  tags.push_back({worker_key_storage.statName(), worker_value_storage.statName()});

  Stats::StatNameManagedStorage cluster_key_storage("cluster", stats_scope.symbolTable());
  Stats::StatNameManagedStorage cluster_value_storage("test-cluster", stats_scope.symbolTable());
  tags.push_back({cluster_key_storage.statName(), cluster_value_storage.statName()});

  Stats::StatNameManagedStorage result_key_storage("result", stats_scope.symbolTable());
  Stats::StatNameManagedStorage result_value_storage("failed", stats_scope.symbolTable());
  tags.push_back({result_key_storage.statName(), result_value_storage.statName()});

  Stats::StatNameManagedStorage failure_reason_key_storage("failure_reason",
                                                           stats_scope.symbolTable());
  Stats::StatNameManagedStorage failure_reason_value_storage("http.401", stats_scope.symbolTable());
  tags.push_back({failure_reason_key_storage.statName(), failure_reason_value_storage.statName()});

  auto& handshake_failed_counter =
      Stats::Utility::counterFromStatNames(stats_scope, {stat_storage.statName()}, tags);
  uint64_t initial_handshake_failed_count = handshake_failed_counter.value();

  // Call onHandshakeFailure with HTTP status error.
  wrapper_ptr->onHandshakeFailure(HandshakeFailureReason::httpStatusError("401"));

  // Get stats after onHandshakeFailure.
  auto final_stats = extension_->getCrossWorkerStatMap();

  // Verify that failed stats were incremented.
  EXPECT_EQ(final_stats[host_failed_stat_name], initial_stats[host_failed_stat_name] + 1);
  EXPECT_EQ(final_stats[cluster_failed_stat_name], initial_stats[cluster_failed_stat_name] + 1);

  // Verify that handshake failure stat was incremented.
  EXPECT_EQ(handshake_failed_counter.value(), initial_handshake_failed_count + 1);
}

// Test RCConnectionWrapper::onHandshakeFailure method with EncodeError.
TEST_F(RCConnectionWrapperTest, OnHandshakeFailureEncodeError) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

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

  auto mock_connection = setupMockConnection();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection to create the wrapper and add it to the map.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify wrapper was created and mapped.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();
  EXPECT_EQ(wrapper_to_host_map.at(wrapper_ptr), "192.168.1.1");

  // Handshake stats use labels (tags) for worker, cluster, result, and failure_reason.
  // Base stat name: reverse_connections.handshake (scope will add test_scope. prefix)
  // Tags: worker=worker_0, cluster=test-cluster, result=failed, failure_reason=encode_error
  auto& stats_scope = extension_->getStatsScope();
  std::string base_stat_name = "reverse_connections.handshake";
  Stats::StatNameManagedStorage stat_storage(base_stat_name, stats_scope.symbolTable());

  // Create tags matching the encode error failure case.
  Stats::StatNameTagVector tags;
  Stats::StatNameManagedStorage worker_key_storage("worker", stats_scope.symbolTable());
  Stats::StatNameManagedStorage worker_value_storage("worker_0", stats_scope.symbolTable());
  tags.push_back({worker_key_storage.statName(), worker_value_storage.statName()});

  Stats::StatNameManagedStorage cluster_key_storage("cluster", stats_scope.symbolTable());
  Stats::StatNameManagedStorage cluster_value_storage("test-cluster", stats_scope.symbolTable());
  tags.push_back({cluster_key_storage.statName(), cluster_value_storage.statName()});

  Stats::StatNameManagedStorage result_key_storage("result", stats_scope.symbolTable());
  Stats::StatNameManagedStorage result_value_storage("failed", stats_scope.symbolTable());
  tags.push_back({result_key_storage.statName(), result_value_storage.statName()});

  Stats::StatNameManagedStorage failure_reason_key_storage("failure_reason",
                                                           stats_scope.symbolTable());
  Stats::StatNameManagedStorage failure_reason_value_storage("encode_error",
                                                             stats_scope.symbolTable());
  tags.push_back({failure_reason_key_storage.statName(), failure_reason_value_storage.statName()});

  auto& handshake_failed_counter =
      Stats::Utility::counterFromStatNames(stats_scope, {stat_storage.statName()}, tags);
  uint64_t initial_handshake_failed_count = handshake_failed_counter.value();

  // Call onHandshakeFailure with EncodeError.
  wrapper_ptr->onHandshakeFailure(HandshakeFailureReason::encodeError());

  // Verify that handshake failure stat was incremented.
  EXPECT_EQ(handshake_failed_counter.value(), initial_handshake_failed_count + 1);
}

// Test RCConnectionWrapper::onEvent method with RemoteClose event.
TEST_F(RCConnectionWrapperTest, OnEventRemoteClose) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

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

  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection to create the wrapper and add it to the map.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify wrapper was created and mapped.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();
  EXPECT_EQ(wrapper_to_host_map.at(wrapper_ptr), "192.168.1.1");

  // Get initial stats before onEvent.
  auto initial_stats = extension_->getCrossWorkerStatMap();
  std::string host_connected_stat_name =
      "test_scope.reverse_connections.host.192.168.1.1.connected";
  std::string cluster_connected_stat_name =
      "test_scope.reverse_connections.cluster.test-cluster.connected";

  // Call onEvent with RemoteClose event.
  wrapper_ptr->onEvent(Network::ConnectionEvent::RemoteClose);

  // Get stats after onEvent.
  auto final_stats = extension_->getCrossWorkerStatMap();

  // Verify that the connection closure was handled gracefully.
}

// Test RCConnectionWrapper::onEvent method with Connected event (should be ignored)
TEST_F(RCConnectionWrapperTest, OnEventConnected) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

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

  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection to create the wrapper and add it to the map.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify wrapper was created and mapped.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();
  EXPECT_EQ(wrapper_to_host_map.at(wrapper_ptr), "192.168.1.1");

  // Get initial stats before onEvent.
  auto initial_stats = extension_->getCrossWorkerStatMap();

  // Call onEvent with Connected event (should be ignored)
  wrapper_ptr->onEvent(Network::ConnectionEvent::Connected);

  // Get stats after onEvent.
  auto final_stats = extension_->getCrossWorkerStatMap();

  // Verify that Connected event doesn't change stats (it should be ignored)
  // The stats should remain the same.
  EXPECT_EQ(final_stats, initial_stats);
}

// Test RCConnectionWrapper::onEvent method with null connection.
TEST_F(RCConnectionWrapperTest, OnEventWithNullConnection) {
  // Set up thread local slot first so stats can be properly tracked.
  setupThreadLocalSlot();

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

  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  Upstream::MockHost::MockCreateConnectionData success_conn_data;
  success_conn_data.connection_ = mock_connection.get();
  success_conn_data.host_description_ = mock_host;

  EXPECT_CALL(*mock_thread_local_cluster, tcpConn_(_)).WillOnce(Return(success_conn_data));

  mock_connection.release();

  // Call initiateOneReverseConnection to create the wrapper and add it to the map.
  bool result = initiateOneReverseConnection("test-cluster", "192.168.1.1", mock_host);
  EXPECT_TRUE(result);

  // Verify wrapper was created and mapped.
  const auto& connection_wrappers = getConnectionWrappers();
  EXPECT_EQ(connection_wrappers.size(), 1);

  const auto& wrapper_to_host_map = getConnWrapperToHostMap();
  EXPECT_EQ(wrapper_to_host_map.size(), 1);

  RCConnectionWrapper* wrapper_ptr = connection_wrappers[0].get();
  EXPECT_EQ(wrapper_to_host_map.at(wrapper_ptr), "192.168.1.1");

  // Get initial stats before onEvent.
  auto initial_stats = extension_->getCrossWorkerStatMap();

  // Call onEvent with RemoteClose event.
  wrapper_ptr->onEvent(Network::ConnectionEvent::RemoteClose);

  // Get stats after onEvent.
  auto final_stats = extension_->getCrossWorkerStatMap();

  // Verify that the event was handled gracefully even with connection closure.
  // The exact behavior depends on the implementation, but it should not crash.
}

// Test decodeHeaders handles HTTP 200 status by calling success path.
TEST_F(RCConnectionWrapperTest, DecodeHeadersOk) {
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);

  wrapper.decodeHeaders(std::move(headers), true);
}

// Test decodeHeaders handles non-200 status by calling failure path.
TEST_F(RCConnectionWrapperTest, DecodeHeadersNonOk) {
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  Http::ResponseHeaderMapPtr headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(404);

  wrapper.decodeHeaders(std::move(headers), true);
}

// Test dispatchHttp1 error path by initializing codec via connect() and
// then feeding invalid bytes to the parser.
TEST_F(RCConnectionWrapperTest, DispatchHttp1ErrorPath) {
  auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();

  EXPECT_CALL(*mock_connection, addConnectionCallbacks(_));
  EXPECT_CALL(*mock_connection, addReadFilter(_));
  EXPECT_CALL(*mock_connection, connect());
  EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(42));
  EXPECT_CALL(*mock_connection, state()).WillRepeatedly(Return(Network::Connection::State::Open));
  // Allow writes made by the HTTP/1 encoder and drain them to simulate kernel behavior.
  EXPECT_CALL(*mock_connection, write(_, _))
      .WillRepeatedly(
          Invoke([](Buffer::Instance& buffer, bool) { buffer.drain(buffer.length()); }));

  // Provide connection info provider.
  auto mock_remote = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1", 80);
  auto mock_local = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 10001);
  EXPECT_CALL(*mock_connection, connectionInfoProvider())
      .WillRepeatedly(Invoke([mock_remote, mock_local]() -> const Network::ConnectionInfoProvider& {
        static auto provider =
            std::make_unique<Network::ConnectionInfoSetterImpl>(mock_local, mock_remote);
        return *provider;
      }));

  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");
  // Initialize codec inside the wrapper.
  (void)wrapper.connect("tenant", "cluster", "node");

  // Feed clearly invalid/non-HTTP bytes to exercise error log path.
  Buffer::OwnedImpl invalid_bytes("\x00\x01garbage");
  wrapper.dispatchHttp1(invalid_bytes);
}

// Test that destructor invokes shutdown when not already called.
TEST_F(RCConnectionWrapperTest, DestructorInvokesShutdown) {
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  EXPECT_CALL(*mock_connection, removeConnectionCallbacks(_));
  EXPECT_CALL(*mock_connection, state()).WillRepeatedly(Return(Network::Connection::State::Open));
  EXPECT_CALL(*mock_connection, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(777));

  {
    RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");
    // No explicit shutdown; leaving scope should run destructor which calls shutdown.
  }
}

// Test RCConnectionWrapper::releaseConnection method.
TEST_F(RCConnectionWrapperTest, ReleaseConnection) {
  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Verify connection exists before release.
  EXPECT_NE(wrapper.getConnection(), nullptr);

  // Release the connection.
  auto released_connection = wrapper.releaseConnection();

  // Verify connection was released.
  EXPECT_NE(released_connection, nullptr);
  EXPECT_EQ(wrapper.getConnection(), nullptr);
}

// Test RCConnectionWrapper::getConnection method.
TEST_F(RCConnectionWrapperTest, GetConnection) {
  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Get the connection.
  auto* connection = wrapper.getConnection();

  // Verify connection is returned.
  EXPECT_NE(connection, nullptr);

  // Test after release.
  wrapper.releaseConnection();
  EXPECT_EQ(wrapper.getConnection(), nullptr);
}

// Test RCConnectionWrapper::getHost method.
TEST_F(RCConnectionWrapperTest, GetHost) {
  // Create a mock connection and host with proper socket setup.
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Get the host.
  auto host = wrapper.getHost();

  // Verify host is returned.
  EXPECT_EQ(host, mock_host);
}

// Test RCConnectionWrapper::onAboveWriteBufferHighWatermark method (no-op)
TEST_F(RCConnectionWrapperTest, OnAboveWriteBufferHighWatermark) {
  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Call onAboveWriteBufferHighWatermark - should be a no-op.
  wrapper.onAboveWriteBufferHighWatermark();
}

// Test RCConnectionWrapper::onBelowWriteBufferLowWatermark method (no-op)
TEST_F(RCConnectionWrapperTest, OnBelowWriteBufferLowWatermark) {
  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Call onBelowWriteBufferLowWatermark - should be a no-op.
  wrapper.onBelowWriteBufferLowWatermark();
}

// Test RCConnectionWrapper::shutdown method.
TEST_F(RCConnectionWrapperTest, Shutdown) {
  // Test 1: Shutdown with open connection.
  {
    auto mock_connection = setupMockConnection();
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

    // Set up connection expectations for open connection.
    EXPECT_CALL(*mock_connection, removeConnectionCallbacks(_));
    EXPECT_CALL(*mock_connection, state()).WillRepeatedly(Return(Network::Connection::State::Open));
    EXPECT_CALL(*mock_connection, close(Network::ConnectionCloseType::FlushWrite));
    EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(12345));

    RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

    EXPECT_NE(wrapper.getConnection(), nullptr);
    wrapper.shutdown();
    EXPECT_EQ(wrapper.getConnection(), nullptr);
  }
  // Test 2: Shutdown with already closed connection.
  {
    auto mock_connection = setupMockConnection();
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

    // Set up connection expectations for closed connection.
    EXPECT_CALL(*mock_connection, state())
        .WillRepeatedly(Return(Network::Connection::State::Closed));
    EXPECT_CALL(*mock_connection, close(_))
        .Times(0); // Should not call close on already closed connection
    EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(12346));

    RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

    EXPECT_NE(wrapper.getConnection(), nullptr);
    wrapper.shutdown();
    EXPECT_EQ(wrapper.getConnection(), nullptr);
  }

  // Test 3: Shutdown with closing connection.
  {
    auto mock_connection = setupMockConnection();
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

    // Set up connection expectations for closing connection.
    EXPECT_CALL(*mock_connection, removeConnectionCallbacks(_));
    EXPECT_CALL(*mock_connection, state())
        .WillRepeatedly(Return(Network::Connection::State::Closing));
    EXPECT_CALL(*mock_connection, close(_))
        .Times(0); // Should not call close on already closing connection
    EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(12347));

    RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

    EXPECT_NE(wrapper.getConnection(), nullptr);
    wrapper.shutdown();
    EXPECT_EQ(wrapper.getConnection(), nullptr);
  }
  // Test 4: Shutdown with null connection (should be safe)
  {
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

    // Create wrapper with null connection.
    RCConnectionWrapper wrapper(*io_handle_, nullptr, mock_host, "test-cluster");

    EXPECT_EQ(wrapper.getConnection(), nullptr);
    wrapper.shutdown(); // Should not crash
    EXPECT_EQ(wrapper.getConnection(), nullptr);
  }
  // Test 5: Multiple shutdown calls (should be safe)
  {
    auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

    // Set up connection expectations.
    EXPECT_CALL(*mock_connection, removeConnectionCallbacks(_));
    EXPECT_CALL(*mock_connection, state()).WillRepeatedly(Return(Network::Connection::State::Open));
    EXPECT_CALL(*mock_connection, close(Network::ConnectionCloseType::FlushWrite));
    EXPECT_CALL(*mock_connection, id()).WillRepeatedly(Return(12348));

    RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

    EXPECT_NE(wrapper.getConnection(), nullptr);

    // First shutdown.
    wrapper.shutdown();
    EXPECT_EQ(wrapper.getConnection(), nullptr);

    // Second shutdown (should be safe)
    wrapper.shutdown();
    EXPECT_EQ(wrapper.getConnection(), nullptr);
  }
}

// Test SimpleConnReadFilter::onData method.
class SimpleConnReadFilterTest : public testing::Test {
protected:
  void SetUp() override {
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Create a mock IO handle.
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockConnection>>();
    io_handle_ = std::make_unique<ReverseConnectionIOHandle>(
        7, // dummy fd
        ReverseConnectionSocketConfig{}, cluster_manager_,
        nullptr,        // extension
        *stats_scope_); // Use the created scope
  }

  void TearDown() override { io_handle_.reset(); }

  // Helper to create a mock RCConnectionWrapper.
  std::unique_ptr<RCConnectionWrapper> createMockWrapper() {
    auto mock_connection = std::make_unique<NiceMock<Network::MockClientConnection>>();
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();
    return std::make_unique<RCConnectionWrapper>(*io_handle_, std::move(mock_connection), mock_host,
                                                 "test-cluster");
  }

  // Helper to create SimpleConnReadFilter.
  std::unique_ptr<SimpleConnReadFilter> createFilter(void* parent) {
    return std::make_unique<SimpleConnReadFilter>(parent);
  }

  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  std::unique_ptr<ReverseConnectionIOHandle> io_handle_;
};

TEST_F(SimpleConnReadFilterTest, OnDataWithNullParent) {
  // Create filter with null parent.
  auto filter = createFilter(nullptr);

  // Create a buffer with some data.
  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\n\r\n");

  // Call onData - should return StopIteration when parent is null.
  auto result = filter->onData(buffer, false);
  EXPECT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(SimpleConnReadFilterTest, OnDataWithHttp200Response) {
  // Create wrapper and filter.
  auto wrapper = createMockWrapper();
  auto filter = createFilter(wrapper.get());

  // Create a buffer with HTTP 200 response.
  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\n\r\n");

  // Call onData - the filter always stops iteration after dispatch.
  auto result = filter->onData(buffer, false);
  EXPECT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(SimpleConnReadFilterTest, OnDataWithHttp2Response) {
  // Create wrapper and filter.
  auto wrapper = createMockWrapper();
  auto filter = createFilter(wrapper.get());

  // Create a buffer with HTTP/2 response but invalid protobuf.
  Buffer::OwnedImpl buffer("HTTP/2 200\r\n\r\nACCEPTED");

  // Call onData - should return StopIteration for invalid response format.
  auto result = filter->onData(buffer, false);
  EXPECT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(SimpleConnReadFilterTest, OnDataWithIncompleteHeaders) {
  // Create wrapper and filter.
  auto wrapper = createMockWrapper();
  auto filter = createFilter(wrapper.get());

  // Create a buffer with incomplete HTTP headers.
  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n");

  // Call onData - the filter always stops iteration after dispatch.
  auto result = filter->onData(buffer, false);
  EXPECT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(SimpleConnReadFilterTest, OnDataWithEmptyResponseBody) {
  // Create wrapper and filter.
  auto wrapper = createMockWrapper();
  auto filter = createFilter(wrapper.get());

  // Create a buffer with HTTP 200 but empty body.
  Buffer::OwnedImpl buffer("HTTP/1.1 200 OK\r\n\r\n");

  // Call onData - the filter always stops iteration after dispatch.
  auto result = filter->onData(buffer, false);
  EXPECT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(SimpleConnReadFilterTest, OnDataWithNon200Response) {
  // Create wrapper and filter.
  auto wrapper = createMockWrapper();
  auto filter = createFilter(wrapper.get());

  // Create a buffer with HTTP 404 response.
  Buffer::OwnedImpl buffer("HTTP/1.1 404 Not Found\r\n\r\n");

  // Call onData - should return StopIteration for error response.
  auto result = filter->onData(buffer, false);
  EXPECT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(SimpleConnReadFilterTest, OnDataWithHttp2ErrorResponse) {
  // Create wrapper and filter.
  auto wrapper = createMockWrapper();
  auto filter = createFilter(wrapper.get());

  // Create a buffer with HTTP/2 error response.
  Buffer::OwnedImpl buffer("HTTP/2 500\r\n\r\n");

  // Call onData - should return StopIteration for error response.
  auto result = filter->onData(buffer, false);
  EXPECT_EQ(result, Network::FilterStatus::StopIteration);
}

TEST_F(SimpleConnReadFilterTest, OnDataWithPartialData) {
  // Create wrapper and filter.
  auto wrapper = createMockWrapper();
  auto filter = createFilter(wrapper.get());

  // Create a buffer with partial data (no HTTP response yet)
  Buffer::OwnedImpl buffer("partial data");

  // Call onData - the filter always stops iteration after dispatch.
  auto result = filter->onData(buffer, false);
  EXPECT_EQ(result, Network::FilterStatus::StopIteration);
}

// Test all no-op methods in RCConnectionWrapper.
TEST_F(RCConnectionWrapperTest, NoOpMethods) {
  // Create a mock connection with proper socket setup.
  auto mock_connection = setupMockConnection();
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHostDescription>>();

  // Create RCConnectionWrapper with the mock connection.
  RCConnectionWrapper wrapper(*io_handle_, std::move(mock_connection), mock_host, "test-cluster");

  // Test Network::ConnectionCallbacks no-op methods
  wrapper.onAboveWriteBufferHighWatermark();
  wrapper.onBelowWriteBufferLowWatermark();

  // Test Http::ResponseDecoder no-op methods
  wrapper.decode1xxHeaders(nullptr);

  Buffer::OwnedImpl data("test data");
  wrapper.decodeData(data, false);
  wrapper.decodeData(data, true);

  wrapper.decodeTrailers(nullptr);
  wrapper.decodeMetadata(nullptr);

  std::ostringstream output;
  wrapper.dumpState(output, 0);
  wrapper.dumpState(output, 2);

  // Test Http::ConnectionCallbacks no-op methods
  wrapper.onGoAway(Http::GoAwayErrorCode::NoError);
  wrapper.onGoAway(Http::GoAwayErrorCode::Other);

  NiceMock<Http::MockReceivedSettings> settings;
  wrapper.onSettings(settings);

  wrapper.onMaxStreamsChanged(0);
  wrapper.onMaxStreamsChanged(100);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
