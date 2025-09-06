#include "envoy/common/optref.h"
#include "envoy/extensions/bootstrap/reverse_connection_handshake/v3/reverse_connection_handshake.pb.h"
#include "envoy/extensions/bootstrap/reverse_connection_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/http/message_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/http/reverse_conn/reverse_conn_filter.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"

// Include reverse connection components for testing
#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_acceptor.h"
#include "source/common/thread_local/thread_local_impl.h"

// Add namespace alias for convenience
namespace ReverseConnection = Envoy::Extensions::Bootstrap::ReverseConnection;

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

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
    EXPECT_CALL(callbacks_, connection())
        .WillRepeatedly(Return(OptRef<const Network::Connection>{connection_}));
    EXPECT_CALL(callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
    EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));

    // Create the configs
    upstream_config_.set_stat_prefix("test_prefix");
    downstream_config_.set_stat_prefix("test_prefix");
  }

  // Helper method to set up upstream extension only
  void setupUpstreamExtension() {
    // Create the upstream socket interface and extension
    upstream_socket_interface_ =
        std::make_unique<ReverseConnection::ReverseTunnelAcceptor>(context_);
    upstream_extension_ = std::make_unique<ReverseConnection::ReverseTunnelAcceptorExtension>(
        *upstream_socket_interface_, context_, upstream_config_);

    // Set up the extension in the global socket interface registry
    auto* registered_upstream_interface = Network::socketInterface(
        "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");
    if (registered_upstream_interface) {
      auto* registered_acceptor = dynamic_cast<ReverseConnection::ReverseTunnelAcceptor*>(
          const_cast<Network::SocketInterface*>(registered_upstream_interface));
      if (registered_acceptor) {
        // Set up the extension for the registered upstream socket interface
        registered_acceptor->extension_ = upstream_extension_.get();
      }
    }
  }

  // Helper method to set up downstream extension only
  void setupDownstreamExtension() {
    // Create the downstream socket interface and extension
    downstream_socket_interface_ =
        std::make_unique<ReverseConnection::ReverseTunnelInitiator>(context_);
    downstream_extension_ = std::make_unique<ReverseConnection::ReverseTunnelInitiatorExtension>(
        context_, downstream_config_);

    // Set up the extension in the global socket interface registry
    auto* registered_downstream_interface = Network::socketInterface(
        "envoy.bootstrap.reverse_connection.downstream_reverse_connection_socket_interface");
    if (registered_downstream_interface) {
      auto* registered_initiator = dynamic_cast<ReverseConnection::ReverseTunnelInitiator*>(
          const_cast<Network::SocketInterface*>(registered_downstream_interface));
      if (registered_initiator) {
        // Set up the extension for the registered downstream socket interface
        registered_initiator->extension_ = downstream_extension_.get();
      }
    }
  }

  // Helper method to set up both upstream and downstream extensions
  void setupExtensions() {
    setupUpstreamExtension();
    setupDownstreamExtension();
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
  Http::TestRequestHeaderMapImpl
  createReverseConnectionRequestHeaders(uint32_t content_length = 100) {
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
  bool testMatchRequestPath(ReverseConnFilter* filter, const std::string& request_path,
                            const std::string& api_path) {
    // Use the friend class access to call the private method
    return filter->matchRequestPath(request_path, api_path);
  }

  // Helper functions to call private methods in ReverseConnFilter
  ReverseConnection::UpstreamSocketManager*
  testGetUpstreamSocketManager(ReverseConnFilter* filter) {
    return filter->getUpstreamSocketManager();
  }

  const ReverseConnection::ReverseTunnelInitiator*
  testGetDownstreamSocketInterface(ReverseConnFilter* filter) {
    return filter->getDownstreamSocketInterface();
  }

  ReverseConnection::ReverseTunnelAcceptorExtension*
  testGetUpstreamSocketInterfaceExtension(ReverseConnFilter* filter) {
    return filter->getUpstreamSocketInterfaceExtension();
  }

  ReverseConnection::ReverseTunnelInitiatorExtension*
  testGetDownstreamSocketInterfaceExtension(ReverseConnFilter* filter) {
    return filter->getDownstreamSocketInterfaceExtension();
  }

  // Helper function to call the private saveDownstreamConnection method
  void testSaveDownstreamConnection(ReverseConnFilter* filter, Network::Connection& connection,
                                    const std::string& node_id, const std::string& cluster_id) {
    filter->saveDownstreamConnection(connection, node_id, cluster_id);
  }

  // Helper function to test the private getQueryParam method
  std::string testGetQueryParam(ReverseConnFilter* filter, const std::string& key) {
    // Call the private method using friend class access
    return filter->getQueryParam(key);
  }

  // Helper function to test the private determineRole method
  std::string testDetermineRole(ReverseConnFilter* filter) { return filter->determineRole(); }

  // Helper function to create a protobuf handshake argument
  std::string createHandshakeArg(const std::string& tenant_uuid, const std::string& cluster_uuid,
                                 const std::string& node_uuid) {
    envoy::extensions::bootstrap::reverse_connection_handshake::v3::ReverseConnHandshakeArg arg;
    arg.set_tenant_uuid(tenant_uuid);
    arg.set_cluster_uuid(cluster_uuid);
    arg.set_node_uuid(node_uuid);
    return arg.SerializeAsString();
  }

  // Helper method to set up upstream thread local slot for testing
  void setupUpstreamThreadLocalSlot() {
    // Call onServerInitialized to set up the extension references properly
    upstream_extension_->onServerInitialized();

    // Create a thread local registry for upstream with the properly initialized extension
    upstream_thread_local_registry_ =
        std::make_shared<ReverseConnection::UpstreamSocketThreadLocal>(dispatcher_,
                                                                       upstream_extension_.get());

    upstream_tls_slot_ =
        ThreadLocal::TypedSlot<ReverseConnection::UpstreamSocketThreadLocal>::makeUnique(
            thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the upstream slot to return our registry
    upstream_tls_slot_->set(
        [registry = upstream_thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Override the TLS slot with our test version
    upstream_extension_->setTestOnlyTLSRegistry(std::move(upstream_tls_slot_));
  }

  // Helper method to set up downstream thread local slot for testing
  void setupDownstreamThreadLocalSlot() {
    // Call onServerInitialized to set up the extension references properly
    downstream_extension_->onServerInitialized();

    // Create a thread local registry for downstream with the dispatcher
    downstream_thread_local_registry_ =
        std::make_shared<ReverseConnection::DownstreamSocketThreadLocal>(dispatcher_,
                                                                         *stats_scope_);

    downstream_tls_slot_ =
        ThreadLocal::TypedSlot<ReverseConnection::DownstreamSocketThreadLocal>::makeUnique(
            thread_local_);

    // Set up the downstream slot to return our registry
    downstream_tls_slot_->set(
        [registry = downstream_thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Override the TLS slot with our test version
    downstream_extension_->setTestOnlyTLSRegistry(std::move(downstream_tls_slot_));
  }

  // Helper method to set up thread local slot for testing
  void setupThreadLocalSlot() {
    setupUpstreamThreadLocalSlot();
    setupDownstreamThreadLocalSlot();
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
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  // Mock socket for testing
  std::unique_ptr<Network::ConnectionSocket> mock_socket_;
  std::unique_ptr<NiceMock<Network::MockIoHandle>> mock_io_handle_;

  // Helper method to set up socket mock with proper expectations for tests
  void setupSocketMock(bool expect_duplicate = true) {
    // Create a mock socket that inherits from ConnectionSocket
    auto mock_socket_ptr = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle_ = std::make_unique<NiceMock<Network::MockIoHandle>>();

    // Set up IO handle expectations
    EXPECT_CALL(*mock_io_handle_, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*mock_io_handle_, isOpen()).WillRepeatedly(Return(true));

    // Only expect duplicate() if the socket will actually be used
    if (expect_duplicate) {
      EXPECT_CALL(*mock_io_handle_, duplicate()).WillOnce(Invoke([&]() {
        auto duplicated_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
        EXPECT_CALL(*duplicated_handle, fdDoNotUse()).WillRepeatedly(Return(124));
        EXPECT_CALL(*duplicated_handle, isOpen()).WillRepeatedly(Return(true));
        EXPECT_CALL(*duplicated_handle, resetFileEvents());
        return duplicated_handle;
      }));
    }

    // Set up socket expectations
    EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_));
    EXPECT_CALL(*mock_socket_ptr, isOpen()).WillRepeatedly(Return(true));

    // Store the mock_io_handle in the socket
    mock_socket_ptr->io_handle_ = std::move(mock_io_handle_);

    // Cast the mock to the base ConnectionSocket type and store it
    mock_socket_ = std::unique_ptr<Network::ConnectionSocket>(mock_socket_ptr.release());

    // Set up connection to return the socket
    EXPECT_CALL(connection_, getSocket()).WillRepeatedly(ReturnRef(mock_socket_));
  }

  // Thread local components for testing upstream socket manager
  std::unique_ptr<ThreadLocal::TypedSlot<ReverseConnection::UpstreamSocketThreadLocal>>
      upstream_tls_slot_;
  std::shared_ptr<ReverseConnection::UpstreamSocketThreadLocal> upstream_thread_local_registry_;
  std::unique_ptr<ReverseConnection::ReverseTunnelAcceptor> upstream_socket_interface_;
  std::unique_ptr<ReverseConnection::ReverseTunnelAcceptorExtension> upstream_extension_;

  std::unique_ptr<ThreadLocal::TypedSlot<ReverseConnection::DownstreamSocketThreadLocal>>
      downstream_tls_slot_;
  std::shared_ptr<ReverseConnection::DownstreamSocketThreadLocal> downstream_thread_local_registry_;
  std::unique_ptr<ReverseConnection::ReverseTunnelInitiator> downstream_socket_interface_;
  std::unique_ptr<ReverseConnection::ReverseTunnelInitiatorExtension> downstream_extension_;

  // Config for reverse connection socket interface
  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface upstream_config_;
  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface downstream_config_;

  // Set debug logging for this test
  LogLevelSetter log_level_setter_{ENVOY_SPDLOG_LEVEL(debug)};

  void TearDown() override {
    // Clean up thread local components
    upstream_tls_slot_.reset();
    upstream_thread_local_registry_.reset();
    upstream_extension_.reset();
    upstream_socket_interface_.reset();

    downstream_tls_slot_.reset();
    downstream_thread_local_registry_.reset();
    downstream_extension_.reset();
    downstream_socket_interface_.reset();
  }

  // Helper method to create an initiated connection for testing
  void createInitiatedConnection(const std::string& node_id, const std::string& cluster_id) {
    // Manually set the gauge values to simulate initiated connections
    setInitiatedConnectionStats(node_id, cluster_id, 1);
  }

  // Helper method to manually set gauge values for testing initiated connections
  void setInitiatedConnectionStats(const std::string& node_id, const std::string& cluster_id,
                                   uint64_t count = 1) {
    // Set cross-worker stats (these are the ones used by getCrossWorkerStatMap)
    auto& stats_store = downstream_extension_->getStatsScope();

    // Set host connection stat - use the pattern expected by getCrossWorkerStatMap
    std::string host_stat_name = fmt::format("reverse_connections.host.{}.connected", node_id);
    auto& host_gauge =
        stats_store.gaugeFromString(host_stat_name, Stats::Gauge::ImportMode::Accumulate);
    host_gauge.set(count);

    // Set cluster connection stat - use the pattern expected by getCrossWorkerStatMap
    std::string cluster_stat_name =
        fmt::format("reverse_connections.cluster.{}.connected", cluster_id);
    auto& cluster_gauge =
        stats_store.gaugeFromString(cluster_stat_name, Stats::Gauge::ImportMode::Accumulate);
    cluster_gauge.set(count);
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

// Test helper functions for socket interface access - Extension not created
TEST_F(ReverseConnFilterTest, SocketInterfaceHelpersNoExtensions) {
  auto filter = createFilter();

  // Test all four helper functions when no extensions are created
  auto* upstream_manager = testGetUpstreamSocketManager(filter.get());
  auto* upstream_extension = testGetUpstreamSocketInterfaceExtension(filter.get());
  auto* downstream_extension = testGetDownstreamSocketInterfaceExtension(filter.get());

  EXPECT_EQ(upstream_manager, nullptr);     // No TLS registry set up
  EXPECT_EQ(upstream_extension, nullptr);   // No extension set up
  EXPECT_EQ(downstream_extension, nullptr); // No extension set up
}

// Test helper functions for socket interface access - Extensions created but no TLS slots
TEST_F(ReverseConnFilterTest, SocketInterfaceHelpersExtensionsNoSlots) {
  auto filter = createFilter();

  // Set up extensions but don't set up TLS slots
  setupExtensions();

  // Test all four helper functions when extensions are created but TLS slots are not set up
  auto* upstream_manager = testGetUpstreamSocketManager(filter.get());
  auto* upstream_extension = testGetUpstreamSocketInterfaceExtension(filter.get());
  auto* downstream_extension = testGetDownstreamSocketInterfaceExtension(filter.get());

  // Upstream manager should be nullptr because TLS registry is not set up
  EXPECT_EQ(upstream_manager, nullptr);

  // Extensions should be found since we created them, but TLS slots are not set up
  EXPECT_NE(upstream_extension, nullptr);
  EXPECT_EQ(upstream_extension, upstream_extension_.get());
  EXPECT_NE(downstream_extension, nullptr);
  EXPECT_EQ(downstream_extension, downstream_extension_.get());
}

// Test helper functions for socket interface access - Extensions and TLS slots set up
TEST_F(ReverseConnFilterTest, SocketInterfaceHelpersExtensionsAndSlots) {
  auto filter = createFilter();

  // Set up extensions and TLS slots
  setupExtensions();
  setupThreadLocalSlot();

  // Test all four helper functions when everything is properly set up
  auto* upstream_manager = testGetUpstreamSocketManager(filter.get());
  auto* downstream_interface = testGetDownstreamSocketInterface(filter.get());
  auto* upstream_extension = testGetUpstreamSocketInterfaceExtension(filter.get());
  auto* downstream_extension = testGetDownstreamSocketInterfaceExtension(filter.get());

  // All should be non-null when properly set up
  EXPECT_NE(upstream_manager, nullptr);
  EXPECT_EQ(upstream_manager, upstream_thread_local_registry_->socketManager());

  EXPECT_NE(downstream_interface, nullptr);

  EXPECT_NE(upstream_extension, nullptr);
  EXPECT_EQ(upstream_extension, upstream_extension_.get());

  EXPECT_NE(downstream_extension, nullptr);
  EXPECT_EQ(downstream_extension, downstream_extension_.get());
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
  EXPECT_TRUE(
      testMatchRequestPath(filter.get(), "/reverse_connections/request", "/reverse_connections"));

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
  // Set up extensions and thread local slot for upstream socket manager
  setupExtensions();
  setupThreadLocalSlot();

  auto filter = createFilter();

  // Create valid protobuf handshake argument
  std::string handshake_arg = createHandshakeArg("tenant-123", "cluster-456", "node-789");

  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength(std::to_string(handshake_arg.length()));

  // Set up socket mock with proper expectations
  setupSocketMock(true); // Expect duplicate() for valid protobuf

  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);

  // Create buffer with protobuf data
  Buffer::OwnedImpl data(handshake_arg);

  // Process data - this should call acceptReverseConnection
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);

  // Verify that the socket was added to the upstream socket manager
  auto* socket_manager = upstream_thread_local_registry_->socketManager();
  ASSERT_NE(socket_manager, nullptr);

  // Try to get the socket for the node - should be available
  auto retrieved_socket = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket, nullptr);

  // Verify stats were updated
  auto* extension = upstream_extension_.get();
  ASSERT_NE(extension, nullptr);

  // Get per-worker stats to verify the connection was counted
  auto stat_map = extension->getPerWorkerStatMap();

  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node-789"], 1);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster-456"], 1);
}

// Test acceptReverseConnection with incomplete protobuf data
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionProtobufIncomplete) {
  // Set up extensions and thread local slot for upstream socket manager
  setupExtensions();
  setupThreadLocalSlot();

  auto filter = createFilter();

  // Set up headers for reverse connection request with large content length
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength("100"); // Expect 100 bytes but only send 10

  // Set up socket mock - expect no duplicate() since the socket won't be used
  setupSocketMock(false);

  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);

  // Create buffer with incomplete protobuf data (less than expected size)
  Buffer::OwnedImpl data("incomplete");

  // Process data - this should return StopIterationAndBuffer waiting for more data
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationAndBuffer);

  // Verify that no socket was added to the upstream socket manager
  auto* socket_manager = upstream_thread_local_registry_->socketManager();
  ASSERT_NE(socket_manager, nullptr);

  auto retrieved_socket = socket_manager->getConnectionSocket("node-789");
  EXPECT_EQ(retrieved_socket, nullptr);
}

// Test acceptReverseConnection with invalid protobuf data
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionInvalidProtobufParseFailure) {
  // Set up extensions and thread local slot for upstream socket manager
  setupExtensions();
  setupThreadLocalSlot();

  auto filter = createFilter();

  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength("43"); // Match the actual data size we'll send

  // Set up socket mock - saveDownstreamConnection is not called since after
  // protobuf unmarshalling since the node_uuid is empty
  setupSocketMock(false);

  // Expect sendLocalReply to be called with BadGateway status
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::BadGateway, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap&)>,
                          const absl::optional<Grpc::Status::GrpcStatus>&, absl::string_view) {
        // Verify the HTTP status code
        EXPECT_EQ(code, Http::Code::BadGateway);

        // Deserialize the protobuf response to check the actual message
        envoy::extensions::bootstrap::reverse_connection_handshake::v3::ReverseConnHandshakeRet ret;
        EXPECT_TRUE(ret.ParseFromString(std::string(body)));
        EXPECT_EQ(ret.status(), envoy::extensions::bootstrap::reverse_connection_handshake::v3::
                                    ReverseConnHandshakeRet::REJECTED);
        EXPECT_EQ(ret.status_message(),
                  "Failed to parse request message or required fields missing");
      }));

  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);

  // Create buffer with invalid protobuf data that can't be parsed
  // Send exactly 43 bytes to match the content length
  Buffer::OwnedImpl data("invalid protobuf data that cannot be parsed");

  // Process data - this should call acceptReverseConnection and fail parsing
  // The filter should return StopIterationNoBuffer and send a local reply
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);

  // Verify that no socket was added to the upstream socket manager
  auto* socket_manager = upstream_thread_local_registry_->socketManager();
  ASSERT_NE(socket_manager, nullptr);

  auto retrieved_socket = socket_manager->getConnectionSocket("node-789");
  EXPECT_EQ(retrieved_socket, nullptr);
}

// Test acceptReverseConnection with empty node_uuid in protobuf
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionEmptyNodeUuid) {
  // Set up extensions and thread local slot for upstream socket manager
  setupExtensions();
  setupThreadLocalSlot();

  auto filter = createFilter();

  // Create protobuf with empty node_uuid
  envoy::extensions::bootstrap::reverse_connection_handshake::v3::ReverseConnHandshakeArg arg;
  arg.set_tenant_uuid("tenant-123");
  arg.set_cluster_uuid("cluster-456");
  arg.set_node_uuid(""); // Empty node_uuid
  std::string handshake_arg = arg.SerializeAsString();

  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength(std::to_string(handshake_arg.length()));

  // Set up socket mock - since the node_uuid is empty, the socket is not saved
  setupSocketMock(false);

  // Expect sendLocalReply to be called with BadGateway status for empty node_uuid
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::BadGateway, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap&)>,
                          const absl::optional<Grpc::Status::GrpcStatus>&, absl::string_view) {
        // Verify the HTTP status code
        EXPECT_EQ(code, Http::Code::BadGateway);

        // Deserialize the protobuf response to check the actual message
        envoy::extensions::bootstrap::reverse_connection_handshake::v3::ReverseConnHandshakeRet ret;
        EXPECT_TRUE(ret.ParseFromString(std::string(body)));
        EXPECT_EQ(ret.status(), envoy::extensions::bootstrap::reverse_connection_handshake::v3::
                                    ReverseConnHandshakeRet::REJECTED);
        EXPECT_EQ(ret.status_message(),
                  "Failed to parse request message or required fields missing");
      }));

  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);

  // Create buffer with protobuf data
  Buffer::OwnedImpl data(handshake_arg);

  // Process data - this should call acceptReverseConnection and reject
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);

  // Check that no stats were recorded for the cluster
  auto* socket_manager = upstream_thread_local_registry_->socketManager();
  ASSERT_NE(socket_manager, nullptr);

  auto* extension = socket_manager->getUpstreamExtension();
  ASSERT_NE(extension, nullptr);

  // Get cross-worker stats to verify no connection was counted
  auto cross_worker_stat_map = upstream_extension_->getCrossWorkerStatMap();
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.clusters.cluster-456"], 0);
}

// Test acceptReverseConnection with SSL certificate information
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionWithSSLCertificate) {
  // Set up extensions and thread local slot for upstream socket manager
  setupExtensions();
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
  setupSocketMock(true); // Expect duplicate() for SSL test

  // Process headers first
  Http::FilterHeadersStatus header_status = filter->decodeHeaders(headers, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);

  // Create buffer with protobuf data
  Buffer::OwnedImpl data(handshake_arg);

  // Process data - this should call acceptReverseConnection
  Http::FilterDataStatus data_status = filter->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);

  // Verify that the socket was added to the upstream socket manager
  auto* socket_manager = upstream_thread_local_registry_->socketManager();
  ASSERT_NE(socket_manager, nullptr);

  // Try to get the socket for the node - should be available
  auto retrieved_socket = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket, nullptr);
}

// Test acceptReverseConnection with multiple sockets for same node
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionMultipleSockets) {
  // Set up extensions and thread local slot for upstream socket manager
  setupExtensions();
  setupThreadLocalSlot();

  auto filter = createFilter();

  // Create valid protobuf handshake argument
  std::string handshake_arg = createHandshakeArg("tenant-123", "cluster-456", "node-789");

  // Set up headers for reverse connection request
  auto headers = createHeaders("POST", "/reverse_connections/request");
  headers.setContentLength(std::to_string(handshake_arg.length()));

  // Set up socket mock for first connection
  setupSocketMock(true);

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

  // Set up socket mock for second connection
  setupSocketMock(true);

  // Process headers for second connection
  Http::FilterHeadersStatus header_status2 = filter2->decodeHeaders(headers2, false);
  EXPECT_EQ(header_status2, Http::FilterHeadersStatus::StopIteration);

  // Create buffer with protobuf data for second connection
  Buffer::OwnedImpl data2(handshake_arg);

  // Process second data - this should call acceptReverseConnection
  Http::FilterDataStatus data_status2 = filter2->decodeData(data2, true);
  EXPECT_EQ(data_status2, Http::FilterDataStatus::StopIterationNoBuffer);

  // Verify that both sockets were added to the upstream socket manager
  auto* socket_manager = upstream_thread_local_registry_->socketManager();
  ASSERT_NE(socket_manager, nullptr);

  // Try to get the first socket for the node
  auto retrieved_socket1 = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket1, nullptr);

  // Try to get the second socket for the node
  auto retrieved_socket2 = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket2, nullptr);

  // Verify stats were updated correctly for multiple connections
  auto* extension = upstream_extension_.get();
  ASSERT_NE(extension, nullptr);

  // Get per-worker stats to verify the connections were counted
  auto stat_map = extension->getPerWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node-789"], 2);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster-456"], 2);
}

// Test acceptReverseConnection with multiple nodes and clusters for cross-worker stats
TEST_F(ReverseConnFilterTest, AcceptReverseConnectionMultipleNodesCrossWorkerStats) {
  // Set up extensions and thread local slot for upstream socket manager
  setupExtensions();
  setupThreadLocalSlot();

  // Create first filter and connection
  auto filter1 = createFilter();
  std::string handshake_arg1 = createHandshakeArg("tenant-123", "cluster-456", "node-789");
  auto headers1 = createHeaders("POST", "/reverse_connections/request");
  headers1.setContentLength(std::to_string(handshake_arg1.length()));

  // Set up socket mock for first connection
  setupSocketMock(true);

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

  // Set up socket mock for second connection
  setupSocketMock(true);

  Http::FilterHeadersStatus header_status2 = filter2->decodeHeaders(headers2, false);
  EXPECT_EQ(header_status2, Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl data2(handshake_arg2);
  Http::FilterDataStatus data_status2 = filter2->decodeData(data2, true);
  EXPECT_EQ(data_status2, Http::FilterDataStatus::StopIterationNoBuffer);

  // Verify that both sockets were added to the upstream socket manager
  auto* socket_manager = upstream_thread_local_registry_->socketManager();
  ASSERT_NE(socket_manager, nullptr);

  // Try to get both sockets
  auto retrieved_socket1 = socket_manager->getConnectionSocket("node-789");
  EXPECT_NE(retrieved_socket1, nullptr);

  auto retrieved_socket2 = socket_manager->getConnectionSocket("node-123");
  EXPECT_NE(retrieved_socket2, nullptr);

  // Verify cross-worker stats were updated correctly for both connections
  auto* extension = upstream_extension_.get();
  ASSERT_NE(extension, nullptr);

  // Get cross-worker stats to verify both connections were counted
  auto cross_worker_stat_map = extension->getCrossWorkerStatMap();
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.nodes.node-789"], 1);
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.clusters.cluster-456"], 1);
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.nodes.node-123"], 1);
  EXPECT_EQ(cross_worker_stat_map["test_scope.reverse_connections.clusters.cluster-789"], 1);
}

// Test saveDownstreamConnection without socket manager initialized
TEST_F(ReverseConnFilterTest, SaveDownstreamConnectionNoSocketManager) {
  // Set up extensions but not thread local slot - socket manager will not be initialized
  setupExtensions();
  auto filter = createFilter();

  // Set up socket mock
  setupSocketMock(false);

  // Call saveDownstreamConnection - should fail since socket manager is not initialized
  testSaveDownstreamConnection(filter.get(), connection_, "node-789", "cluster-456");

  // Check that no stats were recorded since the socket manager is not available
  auto* extension = upstream_extension_.get();
  ASSERT_NE(extension, nullptr);

  // Get per-worker stats to verify no connection was counted
  auto stat_map = extension->getPerWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node-789"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster-456"], 0);
}

// Test saveDownstreamConnection with original socket closure
TEST_F(ReverseConnFilterTest, SaveDownstreamConnectionOriginalSocketClosed) {
  // Set up extensions and thread local slot for upstream socket manager
  setupExtensions();
  setupThreadLocalSlot();

  auto filter = createFilter();

  // Set up socket mock with closed socket
  auto mock_socket_ptr = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
  auto mock_io_handle_ = std::make_unique<NiceMock<Network::MockIoHandle>>();

  // Set up IO handle expectations for closed socket
  EXPECT_CALL(*mock_io_handle_, fdDoNotUse()).WillRepeatedly(Return(123));
  EXPECT_CALL(*mock_io_handle_, isOpen()).WillRepeatedly(Return(false)); // Socket is closed

  // Don't expect duplicate() since socket is closed
  EXPECT_CALL(*mock_io_handle_, duplicate()).Times(0);

  // Set up socket expectations
  EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_));
  EXPECT_CALL(*mock_socket_ptr, isOpen()).WillRepeatedly(Return(false)); // Socket is closed

  // Store the mock_io_handle in the socket
  mock_socket_ptr->io_handle_ = std::move(mock_io_handle_);

  // Cast the mock to the base ConnectionSocket type and store it
  mock_socket_ = std::unique_ptr<Network::ConnectionSocket>(mock_socket_ptr.release());

  // Set up connection to return the socket
  EXPECT_CALL(connection_, getSocket()).WillRepeatedly(ReturnRef(mock_socket_));

  // Call saveDownstreamConnection directly - should fail since socket is closed
  testSaveDownstreamConnection(filter.get(), connection_, "node-789", "cluster-456");

  // Check that no stats were recorded since the socket was closed
  auto* extension = upstream_extension_.get();
  ASSERT_NE(extension, nullptr);

  // Get per-worker stats to verify no connection was counted
  auto stat_map = extension->getPerWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node-789"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster-456"], 0);
}

// Test saveDownstreamConnection with duplicate failure
TEST_F(ReverseConnFilterTest, SaveDownstreamConnectionDuplicateFailure) {
  // Set up extensions and thread local slot for upstream socket manager
  setupExtensions();
  setupThreadLocalSlot();

  auto filter = createFilter();

  // Set up socket mock with duplicate failure
  auto mock_socket_ptr = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
  auto mock_io_handle_ = std::make_unique<NiceMock<Network::MockIoHandle>>();

  // Set up IO handle expectations
  EXPECT_CALL(*mock_io_handle_, fdDoNotUse()).WillRepeatedly(Return(123));
  EXPECT_CALL(*mock_io_handle_, isOpen()).WillRepeatedly(Return(true));

  // Expect duplicate() to fail (return nullptr)
  EXPECT_CALL(*mock_io_handle_, duplicate()).WillOnce(Return(nullptr));

  // Set up socket expectations
  EXPECT_CALL(*mock_socket_ptr, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_));
  EXPECT_CALL(*mock_socket_ptr, isOpen()).WillRepeatedly(Return(true));

  // Store the mock_io_handle in the socket
  mock_socket_ptr->io_handle_ = std::move(mock_io_handle_);

  // Cast the mock to the base ConnectionSocket type and store it
  mock_socket_ = std::unique_ptr<Network::ConnectionSocket>(mock_socket_ptr.release());

  // Set up connection to return the socket
  EXPECT_CALL(connection_, getSocket()).WillRepeatedly(ReturnRef(mock_socket_));

  // Call saveDownstreamConnection directly - should fail since duplicate() returns nullptr
  testSaveDownstreamConnection(filter.get(), connection_, "node-789", "cluster-456");

  // Check that no stats were recorded since the duplicate operation failed
  auto* extension = upstream_extension_.get();
  ASSERT_NE(extension, nullptr);

  // Get per-worker stats to verify no connection was counted
  auto stat_map = extension->getPerWorkerStatMap();
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.node.node-789"], 0);
  EXPECT_EQ(stat_map["test_scope.reverse_connections.worker_0.cluster.cluster-456"], 0);
}

// Test getQueryParam
TEST_F(ReverseConnFilterTest, GetQueryParamAllCases) {
  // Set up extensions and thread local slots to avoid crashes
  setupExtensions();
  setupThreadLocalSlot();

  auto filter = createFilter();

  // Test with existing query parameters - use a reverse-connection path but with GET method to
  // avoid triggering the full logic
  auto headers = createHeaders(
      "GET", "/reverse_connections?node_id=test-node&cluster_id=test-cluster&role=initiator");
  // Call decodeHeaders to properly set up the request headers
  Http::FilterHeadersStatus status = filter->decodeHeaders(headers, true);
  EXPECT_EQ(status, Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(testGetQueryParam(filter.get(), "node_id"), "test-node");
  EXPECT_EQ(testGetQueryParam(filter.get(), "cluster_id"), "test-cluster");
  EXPECT_EQ(testGetQueryParam(filter.get(), "role"), "initiator");

  // Test with non-existent query parameter
  EXPECT_EQ(testGetQueryParam(filter.get(), "non_existent"), "");

  // Test with empty query string
  auto headers_empty = createHeaders("GET", "/reverse_connections");
  auto filter_empty = createFilter();
  Http::FilterHeadersStatus status_empty = filter_empty->decodeHeaders(headers_empty, true);
  EXPECT_EQ(status_empty, Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(testGetQueryParam(filter_empty.get(), "node_id"), "");
  EXPECT_EQ(testGetQueryParam(filter_empty.get(), "cluster_id"), "");
  EXPECT_EQ(testGetQueryParam(filter_empty.get(), "role"), "");
}

// Test determineRole with different interface registration scenarios
TEST_F(ReverseConnFilterTest, DetermineRoleDifferentInterfaceRegistration) {
  // Test with only downstream extension enabled - should return "initiator"
  auto filter_downstream_only = createFilter();
  setupDownstreamExtension();
  setupDownstreamThreadLocalSlot();
  EXPECT_EQ(testDetermineRole(filter_downstream_only.get()), "initiator");

  // Test with both extensions enabled - should return "both"
  auto filter_both = createFilter();
  setupExtensions();
  setupThreadLocalSlot();
  EXPECT_EQ(testDetermineRole(filter_both.get()), "both");
}

// Test GET request with initiator role -  with remote node
TEST_F(ReverseConnFilterTest, GetRequestInitiatorRoleWithRemoteNode) {
  // Set up both extensions
  setupExtensions();
  setupThreadLocalSlot();

  // Create an initiated connection by setting stats
  createInitiatedConnection("test-node", "test-cluster");

  // Now test the GET request
  auto filter = createFilter();

  // Create GET request with initiator role and remote node
  auto headers = createHeaders("GET", "/reverse_connections?role=initiator&node_id=test-node");

  // Expect sendLocalReply to be called with OK status and node-specific stats
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::OK, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap&)>,
                          const absl::optional<Grpc::Status::GrpcStatus>&, absl::string_view) {
        EXPECT_EQ(code, Http::Code::OK);
        // Should return JSON with available_connections for the specific node
        EXPECT_TRUE(body.find("available_connections") != absl::string_view::npos);
        // Should return count of 1 since we manually set the stats
        EXPECT_TRUE(body.find("\"available_connections\":1") != absl::string_view::npos);
      }));

  Http::FilterHeadersStatus status = filter->decodeHeaders(headers, true);
  EXPECT_EQ(status, Http::FilterHeadersStatus::StopIteration);
}

// Test GET request with initiator role - with remote cluster
TEST_F(ReverseConnFilterTest, GetRequestInitiatorRoleWithRemoteCluster) {
  // Set up both extensions
  setupExtensions();
  setupThreadLocalSlot();

  // Create an initiated connection by setting stats
  createInitiatedConnection("test-node", "test-cluster");

  // Now test the GET request
  auto filter = createFilter();

  // Create GET request with initiator role and remote cluster
  auto headers =
      createHeaders("GET", "/reverse_connections?role=initiator&cluster_id=test-cluster");

  // Expect sendLocalReply to be called with OK status and cluster-specific stats
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::OK, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap&)>,
                          const absl::optional<Grpc::Status::GrpcStatus>&, absl::string_view) {
        EXPECT_EQ(code, Http::Code::OK);
        // Should return JSON with available_connections for the specific cluster
        EXPECT_TRUE(body.find("available_connections") != absl::string_view::npos);
        // Should return count of 1 since we manually set the stats
        EXPECT_TRUE(body.find("\"available_connections\":1") != absl::string_view::npos);
      }));

  Http::FilterHeadersStatus status = filter->decodeHeaders(headers, true);
  EXPECT_EQ(status, Http::FilterHeadersStatus::StopIteration);
}

// Test GET request with initiator role - no node/cluster (aggregated stats)
TEST_F(ReverseConnFilterTest, GetRequestInitiatorRoleAggregatedStats) {
  // Set up both extensions
  setupExtensions();
  setupThreadLocalSlot();

  // Create an initiated connection by setting stats
  createInitiatedConnection("test-node", "test-cluster");

  // Now test the GET request
  auto filter = createFilter();

  // Create GET request with initiator role but no node_id or cluster_id
  auto headers = createHeaders("GET", "/reverse_connections?role=initiator");

  // Expect sendLocalReply to be called with OK status and aggregated stats
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::OK, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap&)>,
                          const absl::optional<Grpc::Status::GrpcStatus>&, absl::string_view) {
        EXPECT_EQ(code, Http::Code::OK);
        // Should return JSON with aggregated stats (accepted and connected arrays)
        EXPECT_TRUE(body.find("accepted") != absl::string_view::npos);
        EXPECT_TRUE(body.find("connected") != absl::string_view::npos);
        // Should show test-cluster in the connected array since we set the stats
        EXPECT_TRUE(body.find("test-cluster") != absl::string_view::npos);
      }));

  Http::FilterHeadersStatus status = filter->decodeHeaders(headers, true);
  EXPECT_EQ(status, Http::FilterHeadersStatus::StopIteration);
}

// Test GET request with responder role - upstream extension present, with remote node
TEST_F(ReverseConnFilterTest, GetRequestResponderRoleWithRemoteNode) {
  // Set up both extensions
  setupExtensions();
  setupThreadLocalSlot();

  // Create an accepted connection by sending a reverse connection request
  auto filter1 = createFilter();
  std::string handshake_arg = createHandshakeArg("tenant-123", "cluster-456", "node-789");
  auto headers1 = createHeaders("POST", "/reverse_connections/request");
  headers1.setContentLength(std::to_string(handshake_arg.length()));

  // Set up socket mock for the connection
  setupSocketMock(true);

  // Process the reverse connection request to create an accepted connection
  Http::FilterHeadersStatus header_status = filter1->decodeHeaders(headers1, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl data(handshake_arg);
  Http::FilterDataStatus data_status = filter1->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);

  // Now test the GET request
  auto filter2 = createFilter();

  // Create GET request with responder role and remote node
  auto headers2 = createHeaders("GET", "/reverse_connections?role=responder&node_id=node-789");

  // Expect sendLocalReply to be called with OK status and node-specific stats
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::OK, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap&)>,
                          const absl::optional<Grpc::Status::GrpcStatus>&, absl::string_view) {
        EXPECT_EQ(code, Http::Code::OK);
        // Should return JSON with available_connections for the specific node
        EXPECT_TRUE(body.find("available_connections") != absl::string_view::npos);
        // Should return count of 1 since we created an actual connection
        EXPECT_TRUE(body.find("\"available_connections\":1") != absl::string_view::npos);
      }));

  Http::FilterHeadersStatus status = filter2->decodeHeaders(headers2, true);
  EXPECT_EQ(status, Http::FilterHeadersStatus::StopIteration);
}

// Test GET request with responder role - upstream extension present, with remote cluster
TEST_F(ReverseConnFilterTest, GetRequestResponderRoleWithRemoteCluster) {
  // Set up both extensions
  setupExtensions();
  setupThreadLocalSlot();

  // Create an accepted connection by sending a reverse connection request
  auto filter1 = createFilter();
  std::string handshake_arg = createHandshakeArg("tenant-123", "cluster-456", "node-789");
  auto headers1 = createHeaders("POST", "/reverse_connections/request");
  headers1.setContentLength(std::to_string(handshake_arg.length()));

  // Set up socket mock for the connection
  setupSocketMock(true);

  // Process the reverse connection request to create an accepted connection
  Http::FilterHeadersStatus header_status = filter1->decodeHeaders(headers1, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl data(handshake_arg);
  Http::FilterDataStatus data_status = filter1->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);

  // Now test the GET request
  auto filter2 = createFilter();

  // Create GET request with responder role and remote cluster
  auto headers2 =
      createHeaders("GET", "/reverse_connections?role=responder&cluster_id=cluster-456");

  // Expect sendLocalReply to be called with OK status and cluster-specific stats
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::OK, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap&)>,
                          const absl::optional<Grpc::Status::GrpcStatus>&, absl::string_view) {
        EXPECT_EQ(code, Http::Code::OK);
        // Should return JSON with available_connections for the specific cluster
        EXPECT_TRUE(body.find("available_connections") != absl::string_view::npos);
        // Should return count of 1 since we created an actual connection
        EXPECT_TRUE(body.find("\"available_connections\":1") != absl::string_view::npos);
      }));

  Http::FilterHeadersStatus status = filter2->decodeHeaders(headers2, true);
  EXPECT_EQ(status, Http::FilterHeadersStatus::StopIteration);
}

// Test GET request with responder role - upstream extension present, no node/cluster (aggregated
// stats)
TEST_F(ReverseConnFilterTest, GetRequestResponderRoleAggregatedStats) {
  // Set up both extensions
  setupExtensions();
  setupThreadLocalSlot();

  // Create an accepted connection by sending a reverse connection request
  auto filter1 = createFilter();
  std::string handshake_arg = createHandshakeArg("tenant-123", "cluster-456", "node-789");
  auto headers1 = createHeaders("POST", "/reverse_connections/request");
  headers1.setContentLength(std::to_string(handshake_arg.length()));

  // Set up socket mock for the connection
  setupSocketMock(true);

  // Process the reverse connection request to create an accepted connection
  Http::FilterHeadersStatus header_status = filter1->decodeHeaders(headers1, false);
  EXPECT_EQ(header_status, Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl data(handshake_arg);
  Http::FilterDataStatus data_status = filter1->decodeData(data, true);
  EXPECT_EQ(data_status, Http::FilterDataStatus::StopIterationNoBuffer);

  // Now test the GET request
  auto filter2 = createFilter();

  // Create GET request with responder role but no node_id or cluster_id
  auto headers2 = createHeaders("GET", "/reverse_connections?role=responder");

  // Expect sendLocalReply to be called with OK status and aggregated stats
  EXPECT_CALL(callbacks_, sendLocalReply(Http::Code::OK, _, _, _, _))
      .WillOnce(Invoke([](Http::Code code, absl::string_view body,
                          std::function<void(Http::ResponseHeaderMap&)>,
                          const absl::optional<Grpc::Status::GrpcStatus>&, absl::string_view) {
        EXPECT_EQ(code, Http::Code::OK);
        // Should return JSON with aggregated stats (accepted and connected arrays)
        EXPECT_TRUE(body.find("accepted") != absl::string_view::npos);
        EXPECT_TRUE(body.find("connected") != absl::string_view::npos);
        // Should show cluster-456 in the accepted array since we created an actual connection
        EXPECT_TRUE(body.find("cluster-456") != absl::string_view::npos);
        // Should show node-789 in the connected array since we created an actual connection
        EXPECT_TRUE(body.find("node-789") != absl::string_view::npos);
      }));

  Http::FilterHeadersStatus status = filter2->decodeHeaders(headers2, true);
  EXPECT_EQ(status, Http::FilterHeadersStatus::StopIteration);
}

} // namespace ReverseConn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
