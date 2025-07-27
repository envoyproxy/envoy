#include "source/extensions/filters/http/reverse_conn/reverse_conn_filter.h"

#include "envoy/extensions/filters/http/reverse_conn/v3/reverse_conn.pb.h"
#include "envoy/extensions/bootstrap/reverse_connection_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"

#include "envoy/network/connection.h"
#include "envoy/common/optref.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/network/socket_interface.h"
#include "source/common/protobuf/protobuf.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/test_runtime.h"

// Include reverse connection components for testing
#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_acceptor.h"
#include "source/common/thread_local/thread_local_impl.h"

// Add namespace alias for convenience
namespace ReverseConnection = Envoy::Extensions::Bootstrap::ReverseConnection;

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::ByMove;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ReverseConn {

class ReverseConnFilterTest : public testing::Test {
protected:
  void SetUp() override {
    // Initialize stats scope
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
    
    // Set up the mock context
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cluster_manager_));

    // Set up the mock callbacks
    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));
    EXPECT_CALL(callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));

  }

  // Helper function to create a filter with default config
  std::unique_ptr<ReverseConnFilter> createFilter() {
    envoy::extensions::filters::http::reverse_conn::v3::ReverseConn config;
    config.mutable_ping_interval()->set_value(5); // 5 seconds
    auto filter_config = std::make_shared<ReverseConnFilterConfig>(config);
    auto filter = std::make_unique<ReverseConnFilter>(filter_config);
    filter->setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

  // Helper function to create a filter with custom config
  std::unique_ptr<ReverseConnFilter> createFilterWithConfig(uint32_t ping_interval) {
    envoy::extensions::filters::http::reverse_conn::v3::ReverseConn config;
    config.mutable_ping_interval()->set_value(ping_interval);
    auto filter_config = std::make_shared<ReverseConnFilterConfig>(config);
    auto filter = std::make_unique<ReverseConnFilter>(filter_config);
    filter->setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

  // Helper function to create HTTP headers
  Http::TestRequestHeaderMapImpl createHeaders(const std::string& method, const std::string& path) {
    Http::TestRequestHeaderMapImpl headers;
    headers.setMethod(method);
    headers.setPath(path);
    headers.setHost("example.com");
    return headers;
  }

  // Helper function to create reverse connection request headers
  Http::TestRequestHeaderMapImpl createReverseConnectionRequestHeaders(uint32_t content_length = 100) {
    auto headers = createHeaders("POST", "/reverse_connections/request");
    headers.setContentLength(content_length);
    return headers;
  }

  // Helper function to create reverse connection info request headers
  Http::TestRequestHeaderMapImpl createReverseConnectionInfoHeaders(const std::string& role = "") {
    auto headers = createHeaders("GET", "/reverse_connections");
    if (!role.empty()) {
      headers.addCopy(Http::LowerCaseString("role"), role);
    }
    return headers;
  }

  // Helper function to test the private matchRequestPath method
  bool testMatchRequestPath(ReverseConnFilter* filter, const std::string& request_path, const std::string& api_path) {
    // Use the friend class access to call the private method
    return filter->matchRequestPath(request_path, api_path);
  }

  // Helper function to set up thread local slot for testing upstream socket manager
  void setupThreadLocalSlot() {
    setupThreadLocalSlotForTesting();
  }

  // Helper function to create a mock socket with proper address setup
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

  // Helper function to create a protobuf handshake argument
  std::string createHandshakeArg(const std::string& tenant_uuid, const std::string& cluster_uuid, const std::string& node_uuid) {
    envoy::extensions::filters::http::reverse_conn::v3::ReverseConnHandshakeArg arg;
    arg.set_tenant_uuid(tenant_uuid);
    arg.set_cluster_uuid(cluster_uuid);
    arg.set_node_uuid(node_uuid);
    return arg.SerializeAsString();
  }

  // Helper function to get the upstream socket manager for testing
  ReverseConnection::UpstreamSocketManager* getUpstreamSocketManager() {
    // Use the local socket manager that was created in setupThreadLocalSlot
    if (socket_manager_) {
      return socket_manager_.get();
    }
    
    // Fallback to accessing through the socket interface if available
    auto* upstream_interface = Network::socketInterface(
        "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");
    if (!upstream_interface) {
      return nullptr;
    }
    
    auto* upstream_socket_interface =
        dynamic_cast<const ReverseConnection::ReverseTunnelAcceptor*>(upstream_interface);
    if (!upstream_socket_interface) {
      return nullptr;
    }
    
    auto* tls_registry = upstream_socket_interface->getLocalRegistry();
    if (!tls_registry) {
      return nullptr;
    }
    
    return tls_registry->socketManager();
  }

  // Helper method to set up thread local slot for testing
  void setupThreadLocalSlotForTesting() {
    // Create the config
    config_.set_stat_prefix("test_prefix");

    // Create the socket interface
    socket_interface_ = std::make_unique<ReverseConnection::ReverseTunnelAcceptor>(context_);

    // Create the extension
    extension_ = std::make_unique<ReverseConnection::ReverseTunnelAcceptorExtension>(
        *socket_interface_, context_, config_);

    // First, call onServerInitialized to set up the extension reference properly
    extension_->onServerInitialized();

    // Create a thread local registry with the properly initialized extension
    thread_local_registry_ = std::make_shared<ReverseConnection::UpstreamSocketThreadLocal>(
        dispatcher_, extension_.get());

    // Create the actual TypedSlot
    tls_slot_ = ThreadLocal::TypedSlot<ReverseConnection::UpstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the slot to return our registry
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Use the public setTestOnlyTLSRegistry method instead of accessing private members
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));

    // Create the socket manager
    socket_manager_ = std::make_unique<ReverseConnection::UpstreamSocketManager>(dispatcher_, extension_.get());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Network::MockConnectionSocket> socket_;
  NiceMock<Network::MockIoHandle> io_handle_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::Metadata metadata_;
  NiceMock<Event::MockDispatcher> dispatcher_{"test_dispatcher"};
  
  // Mock socket for testing
  std::unique_ptr<NiceMock<Network::MockConnectionSocket>> mock_socket_;
  std::unique_ptr<NiceMock<Network::MockIoHandle>> mock_io_handle_;
  
  // Helper method to set up socket mock
  void setupSocketMock() {
    mock_socket_ = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    mock_io_handle_ = std::make_unique<NiceMock<Network::MockIoHandle>>();
    
    EXPECT_CALL(*mock_io_handle_, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*mock_socket_, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_));
    mock_socket_->io_handle_ = std::move(mock_io_handle_);
    
    // Set up connection to return the mock socket
    EXPECT_CALL(connection_, getSocket()).WillRepeatedly(ReturnRef(*mock_socket_));
  }
  
  // Thread local components for testing upstream socket manager
  std::unique_ptr<ThreadLocal::TypedSlot<ReverseConnection::UpstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<ReverseConnection::UpstreamSocketThreadLocal> thread_local_registry_;
  std::unique_ptr<ReverseConnection::UpstreamSocketManager> socket_manager_;
  std::unique_ptr<ReverseConnection::ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseConnection::ReverseTunnelAcceptorExtension> extension_;
  
  // Config for reverse connection socket interface
  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::UpstreamReverseConnectionSocketInterface config_;

  void TearDown() override {
    // Clean up thread local components
    tls_slot_.reset();
    thread_local_registry_.reset();
    socket_manager_.reset();
    extension_.reset();
    socket_interface_.reset();
  }
};

// Test basic filter construction and configuration
TEST_F(ReverseConnFilterTest, BasicConstruction) {
  auto filter = createFilter();
  EXPECT_NE(filter, nullptr);
}

// Test filter construction with default config
TEST_F(ReverseConnFilterTest, DefaultConfig) {
  envoy::extensions::filters::http::reverse_conn::v3::ReverseConn config;
  // Don't set ping_interval, should use default
  auto filter_config = std::make_shared<ReverseConnFilterConfig>(config);
  auto filter = std::make_unique<ReverseConnFilter>(filter_config);
  filter->setDecoderFilterCallbacks(callbacks_);
  
  EXPECT_NE(filter, nullptr);
  EXPECT_EQ(filter_config->pingInterval().count(), 2); // Default is 2 seconds
}

// Test filter construction with custom ping interval
TEST_F(ReverseConnFilterTest, CustomPingInterval) {
  auto filter = createFilterWithConfig(10);
  EXPECT_NE(filter, nullptr);
}

// Test filter destruction
TEST_F(ReverseConnFilterTest, FilterDestruction) {
  auto filter = createFilter();
  EXPECT_NE(filter, nullptr);
  
  // Should not crash on destruction
  filter.reset();
  EXPECT_EQ(filter, nullptr);
}

// Test onDestroy method
TEST_F(ReverseConnFilterTest, OnDestroy) {
  auto filter = createFilter();
  EXPECT_NE(filter, nullptr);
  
  // Should not crash when onDestroy is called
  filter->onDestroy();
}

// Test decodeHeaders with non-reverse connection path (should continue)
TEST_F(ReverseConnFilterTest, DecodeHeadersNonReverseConnectionPath) {
  auto filter = createFilter();
  auto headers = createHeaders("GET", "/some/other/path");
  
  Http::FilterHeadersStatus status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(status, Http::FilterHeadersStatus::Continue);
}

// Test decodeHeaders with reverse connection path but wrong method
TEST_F(ReverseConnFilterTest, DecodeHeadersReverseConnectionPathWrongMethod) {
  auto filter = createFilter();
  auto headers = createHeaders("PUT", "/reverse_connections");
  
  Http::FilterHeadersStatus status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(status, Http::FilterHeadersStatus::Continue);
}

// Test config validation with valid ping interval
TEST_F(ReverseConnFilterTest, ConfigValidationValidPingInterval) {
  envoy::extensions::filters::http::reverse_conn::v3::ReverseConn config;
  config.mutable_ping_interval()->set_value(1); // Valid: 1 second
  
  auto filter_config = std::make_shared<ReverseConnFilterConfig>(config);
  EXPECT_EQ(filter_config->pingInterval().count(), 1);
}

// Test config validation with zero ping interval
TEST_F(ReverseConnFilterTest, ConfigValidationZeroPingInterval) {
  envoy::extensions::filters::http::reverse_conn::v3::ReverseConn config;
  config.mutable_ping_interval()->set_value(0); // Zero should use default
  
  auto filter_config = std::make_shared<ReverseConnFilterConfig>(config);
  EXPECT_EQ(filter_config->pingInterval().count(), 2); // Default is 2 seconds
}

// Test config validation with large ping interval
TEST_F(ReverseConnFilterTest, ConfigValidationLargePingInterval) {
  envoy::extensions::filters::http::reverse_conn::v3::ReverseConn config;
  config.mutable_ping_interval()->set_value(3600); // 1 hour
  
  auto filter_config = std::make_shared<ReverseConnFilterConfig>(config);
  EXPECT_EQ(filter_config->pingInterval().count(), 3600);
}

// Test matchRequestPath helper function
TEST_F(ReverseConnFilterTest, MatchRequestPath) {
  auto filter = createFilter();
  
  // Test exact match
  EXPECT_TRUE(testMatchRequestPath(filter.get(), "/reverse_connections", "/reverse_connections"));
  
  // Test prefix match
  EXPECT_TRUE(testMatchRequestPath(filter.get(), "/reverse_connections/request", "/reverse_connections"));
  
  // Test no match
  EXPECT_FALSE(testMatchRequestPath(filter.get(), "/some/other/path", "/reverse_connections"));
  
  // Test empty path
  EXPECT_FALSE(testMatchRequestPath(filter.get(), "", "/reverse_connections"));
  
  // Test shorter path
  EXPECT_FALSE(testMatchRequestPath(filter.get(), "/reverse", "/reverse_connections"));
}

// Test decodeHeaders with POST method for reverse connection accept request
TEST_F(ReverseConnFilterTest, DecodeHeadersPostReverseConnectionAccept) {
  auto filter = createFilter();
  
  // Create headers for POST request to /reverse_connections/request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength("100"); // Set content length for protobuf
  
  Http::FilterHeadersStatus status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(status, Http::FilterHeadersStatus::StopIteration);
  
}

// Test decodeHeaders with POST method for non-accept reverse connection path
TEST_F(ReverseConnFilterTest, DecodeHeadersPostNonAcceptPath) {
  auto filter = createFilter();
  
  // Create headers for POST request to /reverse_connections (not /request)
  auto headers = createHeaders("POST", "/reverse_connections");
  headers.setContentLength("100");
  
  Http::FilterHeadersStatus status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(status, Http::FilterHeadersStatus::Continue);
}

// acceptReverseConnection Tests

// Test acceptReverseConnection with valid protobuf data
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionValidProtobuf) {
  // Set up thread local slot for upstream socket manager
  setupThreadLocalSlot();
  
  auto filter = createFilter();
  
  // Create valid protobuf handshake argument
  std::string handshake_arg = createHandshakeArg("tenant-123", "cluster-456", "node-789");
  
  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength(std::to_string(handshake_arg.length()));
  
  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);
  
  // Create buffer with protobuf data
  Buffer::OwnedImpl data(handshake_arg);
  
  // Process data - this should call acceptReverseConnection
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);
  
  // Verify that the socket was added to the upstream socket manager
  auto* socket_manager = getUpstreamSocketManager();
  ASSERT_NE(socket_manager, nullptr);
  
  // Try to get the socket for the node - should be available
  auto retrieved_socket = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket, nullptr);
  
  // Verify stats were updated
  auto* extension = socket_manager->getUpstreamExtension();
  ASSERT_NE(extension, nullptr);
  
  // Get per-worker stats to verify the connection was counted
  auto stat_map = extension->getPerWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node-789"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster-456"], 1);
}

// Test acceptReverseConnection with invalid protobuf data
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionInvalidProtobuf) {
  auto filter = createFilter();
  
  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength("50");
  
  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);
  
  // Create buffer with invalid protobuf data
  Buffer::OwnedImpl data("invalid protobuf data that cannot be parsed");
  
  // Process data - this should call acceptReverseConnection and fail
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);
  
  // Verify that no socket was added to the upstream socket manager
  auto* socket_manager = getUpstreamSocketManager();
  if (socket_manager) {
    auto retrieved_socket = socket_manager->getConnectionSocket("node-789");
    EXPECT_EQ(retrieved_socket, nullptr);
  }
}

// Test acceptReverseConnection with empty node_uuid in protobuf
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionEmptyNodeUuid) {
  auto filter = createFilter();
  
  // Create protobuf with empty node_uuid
  envoy::extensions::filters::http::reverse_conn::v3::ReverseConnHandshakeArg arg;
  arg.set_tenant_uuid("tenant-123");
  arg.set_cluster_uuid("cluster-456");
  arg.set_node_uuid(""); // Empty node_uuid
  std::string handshake_arg = arg.SerializeAsString();
  
  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength(std::to_string(handshake_arg.length()));
  
  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);
  
  // Create buffer with protobuf data
  Buffer::OwnedImpl data(handshake_arg);
  
  // Process data - this should call acceptReverseConnection and reject
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);
  
  // Verify that no socket was added to the upstream socket manager
  auto* socket_manager = getUpstreamSocketManager();
  if (socket_manager) {
    auto retrieved_socket = socket_manager->getConnectionSocket("");
    EXPECT_EQ(retrieved_socket, nullptr);
  }
}

// Test acceptReverseConnection with SSL certificate information
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionWithSSLCertificate) {
  // Set up thread local slot for upstream socket manager
  setupThreadLocalSlot();
  
  auto filter = createFilter();
  
  // Create valid protobuf handshake argument
  std::string handshake_arg = createHandshakeArg("tenant-123", "cluster-456", "node-789");
  
  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength(std::to_string(handshake_arg.length()));
  
  // Mock SSL connection with certificate
  auto mock_ssl = std::make_shared<NiceMock<Envoy::Ssl::MockConnectionInfo>>();
  std::vector<std::string> dns_sans = {"tenantId=ssl-tenant", "clusterId=ssl-cluster"};
  EXPECT_CALL(*mock_ssl, peerCertificatePresented()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
  
  // Set up connection with SSL
  EXPECT_CALL(connection_, ssl()).WillRepeatedly(Return(mock_ssl));
  
  // Set up socket mock
  setupSocketMock();
  
  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);
  
  // Create buffer with protobuf data
  Buffer::OwnedImpl data(handshake_arg);
  
  // Process data - this should call acceptReverseConnection
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);
  
  // Verify that the socket was added to the upstream socket manager
  // SSL certificate values should override protobuf values
  auto* socket_manager = getUpstreamSocketManager();
  ASSERT_NE(socket_manager, nullptr);
  
  // Try to get the socket for the node - should be available
  auto retrieved_socket = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket, nullptr);
}

// Test acceptReverseConnection with multiple sockets for same node
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionMultipleSockets) {
  // Set up thread local slot for upstream socket manager
  setupThreadLocalSlot();
  
  auto filter = createFilter();
  
  // Create valid protobuf handshake argument
  std::string handshake_arg = createHandshakeArg("tenant-123", "cluster-456", "node-789");
  
  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength(std::to_string(handshake_arg.length()));
  
  // Set up socket mock
  setupSocketMock();
  
  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);
  
  // Create buffer with protobuf data
  Buffer::OwnedImpl data1(handshake_arg);
  
  // Process first data - this should call acceptReverseConnection
  Http::FilterDataStatus data_status1 = filter->decodeData(data1, false);
  EXPECT_EQ(data_status1, Http::FilterDataStatus::StopIterationNoBuffer);
  
  // Create second filter instance for second connection
  auto filter2 = createFilter();
  
  // Set up headers for second reverse connection request
  auto headers2 = createHeaders("POST", "/reverse_connections/request");
  headers2.setContentLength(std::to_string(handshake_arg.length()));
  
  // Process headers for second connection
  Http::FilterHeadersStatus header_status2 = filter2->decodeHeaders(headers2, false);
  EXPECT_EQ(header_status2, Http::FilterHeadersStatus::StopIteration);
  
  // Create buffer with protobuf data for second connection
  Buffer::OwnedImpl data2(handshake_arg);
  
  // Process second data - this should call acceptReverseConnection
  Http::FilterDataStatus data_status2 = filter2->decodeData(data2, true);
  EXPECT_EQ(data_status2, Http::FilterDataStatus::StopIterationNoBuffer);
  
  // Verify that both sockets were added to the upstream socket manager
  auto* socket_manager = getUpstreamSocketManager();
  ASSERT_NE(socket_manager, nullptr);
  
  // Try to get the first socket for the node
  auto retrieved_socket1 = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket1, nullptr);
  
  // Try to get the second socket for the node
  auto retrieved_socket2 = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket2, nullptr);
  
  // Verify stats were updated correctly for multiple connections
  auto* extension = socket_manager->getUpstreamExtension();
  ASSERT_NE(extension, nullptr);
  
  // Get per-worker stats to verify the connections were counted
  auto stat_map = extension->getPerWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node-789"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster-456"], 2);
}

// Test acceptReverseConnection with cross-worker stats verification
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionCrossWorkerStats) {
  // Set up thread local slot for upstream socket manager
  setupThreadLocalSlot();
  
  auto filter = createFilter();
  
  // Create valid protobuf handshake argument
  std::string handshake_arg = createHandshakeArg("tenant-123", "cluster-456", "node-789");
  
  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength(std::to_string(handshake_arg.length()));
  
  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);
  
  // Create buffer with protobuf data
  Buffer::OwnedImpl data(handshake_arg);
  
  // Process data - this should call acceptReverseConnection
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);
  
  // Verify that the socket was added to the upstream socket manager
  auto* socket_manager = getUpstreamSocketManager();
  ASSERT_NE(socket_manager, nullptr);
  
  // Try to get the socket for the node - should be available
  auto retrieved_socket = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket, nullptr);
  
  // Verify cross-worker stats were updated correctly
  auto* extension = socket_manager->getUpstreamExtension();
  ASSERT_NE(extension, nullptr);
  
  // Get cross-worker stats to verify the connection was counted
  auto cross_worker_stat_map = extension->getCrossWorkerStatMap();
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.nodes.node-789"], 1);
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.clusters.cluster-456"], 1);
}

// Test acceptReverseConnection with multiple nodes and clusters for cross-worker stats
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionMultipleNodesCrossWorkerStats) {
  // Set up thread local slot for upstream socket manager
  setupThreadLocalSlot();
  
  // Create first filter and connection
  auto filter1 = createFilter();
  std::string handshake_arg1 = createHandshakeArg("tenant-123", "cluster-456", "node-789");
  auto headers1 = createHeaders("POST", "/reverse_connections/request");
  headers1.setContentLength(std::to_string(handshake_arg1.length()));
  
  Http::FilterHeadersStatus header_status1 = filter1->decodeHeaders(headers1, false);
  EXPECT_EQ(header_status1, Http::FilterHeadersStatus::StopIteration);
  
  Buffer::OwnedImpl data1(handshake_arg1);
  Http::FilterDataStatus data_status1 = filter1->decodeData(data1, true);
  EXPECT_EQ(data_status1, Http::FilterDataStatus::StopIterationNoBuffer);
  
  // Create second filter and connection with different node/cluster
  auto filter2 = createFilter();
  std::string handshake_arg2 = createHandshakeArg("tenant-456", "cluster-789", "node-123");
  auto headers2 = createHeaders("POST", "/reverse_connections/request");
  headers2.setContentLength(std::to_string(handshake_arg2.length()));
  
  Http::FilterHeadersStatus header_status2 = filter2->decodeHeaders(headers2, false);
  EXPECT_EQ(header_status2, Http::FilterHeadersStatus::StopIteration);
  
  Buffer::OwnedImpl data2(handshake_arg2);
  Http::FilterDataStatus data_status2 = filter2->decodeData(data2, true);
  EXPECT_EQ(data_status2, Http::FilterDataStatus::StopIterationNoBuffer);
  
  // Verify that both sockets were added to the upstream socket manager
  auto* socket_manager = getUpstreamSocketManager();
  ASSERT_NE(socket_manager, nullptr);
  
  // Try to get both sockets
  auto retrieved_socket1 = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket1, nullptr);
  
  auto retrieved_socket2 = socket_manager->getConnectionSocket("node-123");
  EXPECT_NE(retrieved_socket2, nullptr);
  
  // Verify cross-worker stats were updated correctly for both connections
  auto* extension = socket_manager->getUpstreamExtension();
  ASSERT_NE(extension, nullptr);
  
  // Get cross-worker stats to verify both connections were counted
  auto cross_worker_stat_map = extension->getCrossWorkerStatMap();
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.nodes.node-789"], 1);
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.clusters.cluster-456"], 1);
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.nodes.node-123"], 1);
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.clusters.cluster-789"], 1);
}

} // namespace ReverseConn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy 