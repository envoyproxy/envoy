#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"

#include "source/common/config/metadata.h"
#include "source/common/config/utility.h"
#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/singleton/threadsafe_singleton.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/clusters/reverse_connection/reverse_connection.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"
#include "source/server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

class TestLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  TestLoadBalancerContext(const Network::Connection* connection)
      : TestLoadBalancerContext(connection, nullptr) {}
  TestLoadBalancerContext(const Network::Connection* connection,
                          StreamInfo::StreamInfo* request_stream_info)
      : connection_(connection), request_stream_info_(request_stream_info) {}
  TestLoadBalancerContext(const Network::Connection* connection, const std::string& key,
                          const std::string& value)
      : TestLoadBalancerContext(connection) {
    downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{key, value}}};
  }

  // Upstream::LoadBalancerContext.
  absl::optional<uint64_t> computeHashKey() override { return 0; }
  const Network::Connection* downstreamConnection() const override { return connection_; }
  StreamInfo::StreamInfo* requestStreamInfo() const override { return request_stream_info_; }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return downstream_headers_.get();
  }

  absl::optional<uint64_t> hash_key_;
  const Network::Connection* connection_;
  StreamInfo::StreamInfo* request_stream_info_;
  Http::RequestHeaderMapPtr downstream_headers_;
};

class ReverseConnectionClusterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  ReverseConnectionClusterTest() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(server_context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(server_context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));

    // Allow timers and file events to be created multiple times during tests.
    ON_CALL(server_context_.dispatcher_, createTimer_(_))
        .WillByDefault(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    ON_CALL(server_context_.dispatcher_, createFileEvent_(_, _, _, _))
        .WillByDefault(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());

    // Create the config.
    config_.set_stat_prefix("test_prefix");
  }

  ~ReverseConnectionClusterTest() override = default;

  // Set up the upstream extension components (socket interface and extension).
  void setupUpstreamExtension() {
    // Create the socket interface.
    socket_interface_ =
        std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptor>(server_context_);

    // Create the extension.
    extension_ = std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptorExtension>(
        *socket_interface_, server_context_, config_);

    // Get the registered socket interface from the global registry and set up its extension.
    auto* registered_socket_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    if (registered_socket_interface) {
      auto* registered_acceptor = dynamic_cast<BootstrapReverseConnection::ReverseTunnelAcceptor*>(
          const_cast<Network::SocketInterface*>(registered_socket_interface));
      if (registered_acceptor) {
        // Set up the extension for the registered socket interface.
        registered_acceptor->extension_ = extension_.get();
      }
    }
  }

  void setupFromYaml(const std::string& yaml, bool expect_success = true) {
    if (expect_success) {
      cleanup_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
      EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
      EXPECT_CALL(*cleanup_timer_, disableTimer()).Times(testing::AnyNumber());
      EXPECT_CALL(initialized_, ready());
    }
    setup(Upstream::parseClusterFromV3Yaml(yaml));
  }

  void setup(const envoy::config::cluster::v3::Cluster& cluster_config) {

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);

    RevConClusterFactory factory;

    // Parse the ReverseConnectionClusterConfig from the cluster's typed_config.
    envoy::extensions::clusters::reverse_connection::v3::ReverseConnectionClusterConfig
        rev_con_config;
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config.cluster_type().typed_config(), validation_visitor_, rev_con_config));

    auto status_or_pair =
        factory.createClusterWithConfig(cluster_config, rev_con_config, factory_context);
    THROW_IF_NOT_OK_REF(status_or_pair.status());

    cluster_ = std::dynamic_pointer_cast<RevConCluster>(status_or_pair.value().first);
    priority_update_cb_ = cluster_->prioritySet().addPriorityUpdateCb(
        [&](uint32_t, const Upstream::HostVector&, const Upstream::HostVector&) {
          membership_updated_.ready();
          return absl::OkStatus();
        });
    ON_CALL(initialized_, ready()).WillByDefault(testing::Invoke([this] {
      init_complete_ = true;
    }));
    cluster_->initialize([&]() {
      initialized_.ready();
      return absl::OkStatus();
    });
  }

  void TearDown() override {
    // Do not assert on timer teardown; allow destructor-time disable.

    // Clean up thread local resources if they were set up.
    if (tls_slot_) {
      tls_slot_.reset();
    }
    // Don't reset thread_local_registry_ as it's owned by the extension.
    if (extension_) {
      extension_.reset();
    }
    if (socket_interface_) {
      socket_interface_.reset();
    }
  }

  // Helper function to set up thread local slot for tests.
  void setupThreadLocalSlot() {
    // Check if extension is set up.
    if (!extension_) {
      return;
    }

    // Let the extension create and own its TLS slot and manager to avoid duplicate timer/file
    // event creation.
    extension_->onServerInitialized();
  }

  // Helper to add a socket to the manager for testing.
  void addTestSocket(const std::string& node_id, const std::string& cluster_id) {
    if (!socket_interface_) {
      return;
    }

    auto* local_registry = socket_interface_->getLocalRegistry();
    if (local_registry == nullptr || local_registry->socketManager() == nullptr) {
      return;
    }

    // Create a mock socket. Timer and file event creation will use the ON_CALL defaults.
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();
    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));
    socket->io_handle_ = std::move(mock_io_handle);

    // Get the socket manager from the thread local registry.
    auto* tls_socket_manager = local_registry->socketManager();
    EXPECT_NE(tls_socket_manager, nullptr);

    // Add the socket to the manager.
    tls_socket_manager->addConnectionSocket(node_id, cluster_id, std::move(socket),
                                            std::chrono::seconds(30));
  }

  // Helper method to call cleanup since this class is a friend of RevConCluster.
  void callCleanup() { cluster_->cleanup(); }

  // Helper method to create LoadBalancerFactory instance for testing.
  std::unique_ptr<RevConCluster::LoadBalancerFactory> createLoadBalancerFactory() {
    return std::make_unique<RevConCluster::LoadBalancerFactory>(cluster_);
  }

  // Helper method to create ThreadAwareLoadBalancer instance for testing.
  std::unique_ptr<RevConCluster::ThreadAwareLoadBalancer> createThreadAwareLoadBalancer() {
    return std::make_unique<RevConCluster::ThreadAwareLoadBalancer>(cluster_);
  }

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

  std::shared_ptr<RevConCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  Event::MockTimer* cleanup_timer_;
  ::Envoy::Common::CallbackHandlePtr priority_update_cb_;
  bool init_complete_{false};

  // Real thread local slot and registry for reverse connection testing.
  std::unique_ptr<ThreadLocal::TypedSlot<BootstrapReverseConnection::UpstreamSocketThreadLocal>>
      tls_slot_;

  // Real socket interface and extension.
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptorExtension> extension_;

  // Mock thread local instance.
  NiceMock<ThreadLocal::MockInstance> thread_local_;

  // Mock dispatcher.
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  // Stats and config.
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;
};

// Test cluster creation with valid config.
TEST(ReverseConnectionClusterConfigTest, ValidConfig) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
  EXPECT_TRUE(cluster_config.has_cluster_type());
  EXPECT_EQ(cluster_config.cluster_type().name(), "envoy.clusters.reverse_connection");
}

// Test cluster creation failure due to invalid load assignment.
TEST_F(ReverseConnectionClusterTest, BadConfigWithLoadAssignment) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
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
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                            "Reverse Conn clusters must have no load assignment configured");
}

// Test cluster creation failure due to wrong load balancing policy.
TEST_F(ReverseConnectionClusterTest, BadConfigWithWrongLbPolicy) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: ROUND_ROBIN
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                            "cluster: LB policy ROUND_ROBIN is not valid for Cluster type "
                            "envoy.clusters.reverse_connection. Only 'CLUSTER_PROVIDED' is allowed "
                            "with cluster type 'REVERSE_CONNECTION'");
}

TEST_F(ReverseConnectionClusterTest, BasicSetup) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  EXPECT_CALL(membership_updated_, ready()).Times(0);
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

// Test host creation failure due to no context.
TEST_F(ReverseConnectionClusterTest, NoContext) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

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
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }

  // Test null context. It should return a nullptr.
  {
    RevConCluster::LoadBalancer lb(cluster_);
    Upstream::HostConstSharedPtr host = lb.chooseHost(nullptr).host;
    EXPECT_EQ(host, nullptr);
  }
}

// Test host creation failure due to no headers.
TEST_F(ReverseConnectionClusterTest, NoHeaders) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Downstream connection but no headers => no host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }
}

// Test host creation failure due to missing required headers.
TEST_F(ReverseConnectionClusterTest, MissingRequiredHeaders) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Request with unsupported headers but missing all required headers (EnvoyDstNodeUUID,.
  // EnvoyDstClusterUUID, proper Host header).
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-random-header", "random-value");
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }

  // Test with empty header value. This should be skipped and continue to next header.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-remote-node-id", "");
    RevConCluster::LoadBalancer lb(cluster_);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }
}

// Test that validates bootstrap extension must be set up before use.
TEST_F(ReverseConnectionClusterTest, RequiresBootstrapExtension) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Verify cluster was created successfully since bootstrap extension exists.
  EXPECT_NE(cluster_, nullptr);
}

// Test when the socket interface is not registered. In this case, cluster creation should fail.
TEST_F(ReverseConnectionClusterTest, SocketInterfaceNotRegistered) {
  // Temporarily remove the upstream reverse connection socket interface from the registry.
  // This will make Network::socketInterface() return nullptr for the specific name.
  auto saved_factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();

  // Find and remove the specific socket interface factory.
  auto& factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();
  auto it = factories.find("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  if (it != factories.end()) {
    factories.erase(it);
  }

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  // Cluster creation should fail with a clear error message.
  EXPECT_THROW_WITH_MESSAGE(
      setupFromYaml(yaml, false), EnvoyException,
      "Reverse connection cluster requires the upstream reverse tunnel bootstrap extension "
      "'envoy.bootstrap.reverse_tunnel.upstream_socket_interface' to be configured. Please add it "
      "to bootstrap_extensions in your bootstrap configuration.");

  // Restore the registry.
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories() =
      saved_factories;
}

// Test when the socket interface is registered but is the wrong type.
TEST_F(ReverseConnectionClusterTest, SocketInterfaceWrongType) {
  // Create a mock bootstrap extension factory that is NOT a ReverseTunnelAcceptor.
  class WrongTypeFactory : public Server::Configuration::BootstrapExtensionFactory {
  public:
    std::string name() const override {
      return "envoy.bootstrap.reverse_tunnel.upstream_socket_interface";
    }

    Server::BootstrapExtensionPtr
    createBootstrapExtension(const Protobuf::Message&,
                             Server::Configuration::ServerFactoryContext&) override {
      return nullptr;
    }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }
  };

  // Save current factories.
  auto saved_factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();

  // Register the wrong type factory.
  WrongTypeFactory wrong_factory;
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories()
      ["envoy.bootstrap.reverse_tunnel.upstream_socket_interface"] = &wrong_factory;

  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  // Cluster creation should fail because the factory is not a ReverseTunnelAcceptor.
  EXPECT_THROW_WITH_MESSAGE(
      setupFromYaml(yaml, false), EnvoyException,
      "Bootstrap extension 'envoy.bootstrap.reverse_tunnel.upstream_socket_interface' exists but "
      "is not of the expected type (ReverseTunnelAcceptor). This indicates a configuration error.");

  // Restore the registry.
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories() =
      saved_factories;
}

// Test host creation with socket manager.
TEST_F(ReverseConnectionClusterTest, HostCreationWithSocketManager) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  // Set up the thread local slot, initializing the socket manager.
  setupThreadLocalSlot();

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Test host creation with Host header.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "test-uuid-123");
  }

  // Test host creation with header mapping to a different node id (test-uuid-456).
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-456"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "test-uuid-456");
  }

  // Test host creation with HTTP headers.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-remote-node-id", "test-uuid-123");

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "test-uuid-123");
  }
}

TEST_F(ReverseConnectionClusterTest, HostIdExtractionWithSafeRegex) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Initialize upstream extension and thread local registry for socket manager.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Provide reverse connection sockets for different host_ids.
  addTestSocket("foo.bar", "cluster-foo-bar");
  addTestSocket("node-123", "cluster-node-123");

  RevConCluster::LoadBalancer lb(cluster_);

  // Request: header value "foo.bar" routes to host_id "foo.bar".
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "foo.bar"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "foo.bar");
  }

  // Request: header value "node-123" routes to host_id "node-123".
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "node-123"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "node-123");
  }

  // Request: header value "unknown-node" creates a host even without socket.
  // The socket availability check happens at connection time, not host selection time.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "unknown-node"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "unknown-node");
  }
}

// Test host reuse for requests with same UUID.
TEST_F(ReverseConnectionClusterTest, HostReuse) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Add test socket to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);

    // Create second host with same UUID. We should reuse the same host.
    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    EXPECT_EQ(result1.host, result2.host);
  }
}

// Test different hosts for different UUIDs.
TEST_F(ReverseConnectionClusterTest, DifferentHostsForDifferentUUIDs) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);

    // Create second host with different UUID. We should use a different host.
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-456"}}};
    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    EXPECT_NE(result1.host, result2.host);
  }
}

// Test cleanup of hosts.
TEST_F(ReverseConnectionClusterTest, TestCleanup) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create two hosts.
  Upstream::HostSharedPtr host1, host2;

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);
    host1 = std::const_pointer_cast<Upstream::Host>(result1.host);
  }

  // Create second host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-456"}}};

    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    host2 = std::const_pointer_cast<Upstream::Host>(result2.host);
  }

  // Verify hosts are different.
  EXPECT_NE(host1, host2);

  // Expect the cleanup timer to be enabled after cleanup.
  EXPECT_CALL(*cleanup_timer_, enableTimer(std::chrono::milliseconds(10000), nullptr));

  // Call cleanup via the helper method.
  callCleanup();

  // Verify that hosts can still be accessed after cleanup.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
  }
}

// Test cleanup of hosts with used hosts.
TEST_F(ReverseConnectionClusterTest, TestCleanupWithUsedHosts) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Add test sockets to the socket manager.
  addTestSocket("test-uuid-123", "cluster-123");
  addTestSocket("test-uuid-456", "cluster-456");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create two hosts.
  Upstream::HostSharedPtr host1, host2;

  // Create first host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result1 = lb.chooseHost(&lb_context);
    EXPECT_NE(result1.host, nullptr);
    host1 = std::const_pointer_cast<Upstream::Host>(result1.host);
  }

  // Create second host.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-456"}}};

    auto result2 = lb.chooseHost(&lb_context);
    EXPECT_NE(result2.host, nullptr);
    host2 = std::const_pointer_cast<Upstream::Host>(result2.host);
  }

  // Mark one host as used by acquiring a handle.
  auto handle1 = host1->acquireHandle();
  EXPECT_TRUE(host1->used());

  // Expect the cleanup timer to be enabled after cleanup.
  EXPECT_CALL(*cleanup_timer_, enableTimer(std::chrono::milliseconds(10000), nullptr));

  // Call cleanup via the helper method.
  callCleanup();

  // Verify that the used host is still accessible after cleanup.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-remote-node-id", "test-uuid-123"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_NE(result.host, nullptr);
  }

  // Release the handle.
  handle1.reset();
}

// LoadBalancerFactory tests.
TEST_F(ReverseConnectionClusterTest, LoadBalancerFactory) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Test LoadBalancerFactory using helper method.
  auto factory = createLoadBalancerFactory();
  EXPECT_NE(factory, nullptr);

  // Test that the factory creates load balancers.
  Upstream::LoadBalancerParams params{cluster_->prioritySet()};
  auto lb = factory->create(params);
  EXPECT_NE(lb, nullptr);

  // Test that multiple load balancers are different instances.
  auto lb2 = factory->create(params);
  EXPECT_NE(lb2, nullptr);
  EXPECT_NE(lb.get(), lb2.get());

  // Test create() without parameters.
  auto lb3 = factory->create();
  EXPECT_NE(lb3, nullptr);
}

// ThreadAwareLoadBalancer tests
TEST_F(ReverseConnectionClusterTest, ThreadAwareLoadBalancer) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  // Set up the upstream extension for this test
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Test ThreadAwareLoadBalancer using helper method.
  auto thread_aware_lb = createThreadAwareLoadBalancer();
  EXPECT_NE(thread_aware_lb, nullptr);

  // Test initialize() method.
  auto init_status = thread_aware_lb->initialize();
  EXPECT_TRUE(init_status.ok());

  // Test factory() method.
  auto factory = thread_aware_lb->factory();
  EXPECT_NE(factory, nullptr);

  // Test that factory creates load balancers.
  Upstream::LoadBalancerParams params{cluster_->prioritySet()};
  auto lb = factory->create(params);
  EXPECT_NE(lb, nullptr);
}

// Test no-op methods for load balancer.
TEST_F(ReverseConnectionClusterTest, LoadBalancerNoopMethods) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-remote-node-id)%"
  )EOF";

  setupFromYaml(yaml);

  RevConCluster::LoadBalancer lb(cluster_);

  // Test peekAnotherHost. It should return a nullptr.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    Upstream::HostConstSharedPtr peeked_host = lb.peekAnotherHost(&lb_context);
    EXPECT_EQ(peeked_host, nullptr);
  }

  // Test selectExistingConnection. It should return a nullopt.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    std::vector<uint8_t> hash_key;

    // Create a mock host for testing.
    auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
    auto selected_connection = lb.selectExistingConnection(&lb_context, *mock_host, hash_key);
    EXPECT_FALSE(selected_connection.has_value());
  }

  // Test lifetimeCallbacks. It should return an empty OptRef.
  {
    auto lifetime_callbacks = lb.lifetimeCallbacks();
    EXPECT_FALSE(lifetime_callbacks.has_value());
  }
}

// UpstreamReverseConnectionAddress tests
class UpstreamReverseConnectionAddressTest : public testing::Test {
public:
  UpstreamReverseConnectionAddressTest() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(server_context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(server_context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
  }

  void SetUp() override {}

  void TearDown() override {
    // Clean up thread local resources if they were set up.
    if (tls_slot_) {
      tls_slot_.reset();
    }
    // Don't reset thread_local_registry_ as it's owned by the extension.
    if (extension_) {
      extension_.reset();
    }
    if (socket_interface_) {
      socket_interface_.reset();
    }
  }

  // Set up the upstream extension components (socket interface and extension).
  void setupUpstreamExtension() {
    // Create the socket interface.
    socket_interface_ =
        std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptor>(server_context_);

    // Create the extension.
    extension_ = std::make_unique<BootstrapReverseConnection::ReverseTunnelAcceptorExtension>(
        *socket_interface_, server_context_, config_);
  }

  // Set up the thread local slot with the extension.
  void setupThreadLocalSlot() {
    // Check if extension is set up
    if (!extension_) {
      return;
    }

    // Let the extension create and manage its own TLS slot and timer.
    extension_->onServerInitialized();

    // Get the registered socket interface from the global registry and set up its extension.
    auto* registered_socket_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    if (registered_socket_interface) {
      auto* registered_acceptor = dynamic_cast<BootstrapReverseConnection::ReverseTunnelAcceptor*>(
          const_cast<Network::SocketInterface*>(registered_socket_interface));
      if (registered_acceptor) {
        // Set up the extension for the registered socket interface.
        registered_acceptor->extension_ = extension_.get();
      }
    }
  }

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

  // Real thread local slot and registry for reverse connection testing.
  std::unique_ptr<ThreadLocal::TypedSlot<BootstrapReverseConnection::UpstreamSocketThreadLocal>>
      tls_slot_;

  // Real socket interface and extension.
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<BootstrapReverseConnection::ReverseTunnelAcceptorExtension> extension_;

  // Configuration for the extension.
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  // Stats store and scope.
  Stats::TestUtil::TestStore stats_store_;
  Stats::ScopeSharedPtr stats_scope_;

  // Thread local mock.
  NiceMock<ThreadLocal::MockInstance> thread_local_;
};

TEST_F(UpstreamReverseConnectionAddressTest, BasicSetup) {
  const std::string node_id = "test-node-123";
  UpstreamReverseConnectionAddress address(node_id);

  // Test basic properties.
  EXPECT_EQ(address.asString(), "127.0.0.1:0");
  EXPECT_EQ(address.asStringView(), "127.0.0.1:0");
  EXPECT_EQ(address.logicalName(), node_id);
  EXPECT_EQ(address.type(), Network::Address::Type::Ip);
  EXPECT_EQ(address.addressType(), "default");
  EXPECT_FALSE(address.networkNamespace().has_value());
}

TEST_F(UpstreamReverseConnectionAddressTest, EqualityOperator) {
  UpstreamReverseConnectionAddress address1("node-1");
  UpstreamReverseConnectionAddress address2("node-1");
  UpstreamReverseConnectionAddress address3("node-2");

  // Same node ID should be equal.
  EXPECT_TRUE(address1 == address2);
  EXPECT_TRUE(address2 == address1);

  // Different node IDs should not be equal.
  EXPECT_FALSE(address1 == address3);
  EXPECT_FALSE(address3 == address1);

  // Test with different address types.
  Network::Address::Ipv4Instance ipv4_address("127.0.0.1", 8080);
  EXPECT_FALSE(address1 == ipv4_address);
}

TEST_F(UpstreamReverseConnectionAddressTest, SocketAddressMethods) {
  UpstreamReverseConnectionAddress address("test-node");

  // Test sockAddr and sockAddrLen.
  const sockaddr* sock_addr = address.sockAddr();
  EXPECT_NE(sock_addr, nullptr);

  socklen_t addr_len = address.sockAddrLen();
  EXPECT_EQ(addr_len, sizeof(struct sockaddr_in));

  // Verify the socket address structure.
  const struct sockaddr_in* addr_in = reinterpret_cast<const struct sockaddr_in*>(sock_addr);
  EXPECT_EQ(addr_in->sin_family, AF_INET);
  EXPECT_EQ(ntohs(addr_in->sin_port), 0);
  EXPECT_EQ(ntohl(addr_in->sin_addr.s_addr), 0x7f000001); // 127.0.0.1
}

// Test IP-related methods for UpstreamReverseConnectionAddress.
TEST_F(UpstreamReverseConnectionAddressTest, IPMethods) {
  UpstreamReverseConnectionAddress address("test-node");

  // Test IP-related methods.
  const Network::Address::Ip* ip = address.ip();
  EXPECT_NE(ip, nullptr);

  // Test IP address properties.
  EXPECT_EQ(ip->addressAsString(), "0.0.0.0:0");
  EXPECT_TRUE(ip->isAnyAddress());
  EXPECT_FALSE(ip->isUnicastAddress());
  EXPECT_EQ(ip->port(), 0);
  EXPECT_EQ(ip->version(), Network::Address::IpVersion::v4);

  // Test additional IP methods.
  EXPECT_FALSE(ip->isLinkLocalAddress());
  EXPECT_FALSE(ip->isUniqueLocalAddress());
  EXPECT_FALSE(ip->isSiteLocalAddress());
  EXPECT_FALSE(ip->isTeredoAddress());

  // Test IPv4/IPv6 methods.
  EXPECT_EQ(ip->ipv4(), nullptr);
  EXPECT_EQ(ip->ipv6(), nullptr);
}

TEST_F(UpstreamReverseConnectionAddressTest, PipeAndInternalAddressMethods) {
  UpstreamReverseConnectionAddress address("test-node");

  // Test pipe and internal address methods.
  EXPECT_EQ(address.pipe(), nullptr);
  EXPECT_EQ(address.envoyInternalAddress(), nullptr);
}

// Test socketInterface() functionality for UpstreamReverseConnectionAddress.
TEST_F(UpstreamReverseConnectionAddressTest, SocketInterfaceWithAvailableInterface) {
  // Set up the upstream extension and thread local slot.
  setupUpstreamExtension();
  setupThreadLocalSlot();

  // Create an address instance.
  UpstreamReverseConnectionAddress address("test-node");
  const Network::SocketInterface& socket_interface = address.socketInterface();

  // Should return the upstream reverse connection socket interface.
  EXPECT_NE(&socket_interface, nullptr);

  // Verify that the returned interface is of type ReverseTunnelAcceptor.
  const auto* reverse_tunnel_acceptor =
      dynamic_cast<const BootstrapReverseConnection::ReverseTunnelAcceptor*>(&socket_interface);
  EXPECT_NE(reverse_tunnel_acceptor, nullptr);
}

// Test socketInterface() functionality when the upstream socket interface is not found.
TEST_F(UpstreamReverseConnectionAddressTest, SocketInterfaceWithUnavailableInterface) {
  // Temporarily remove the upstream reverse connection socket interface from the registry
  // This will make Network::socketInterface() return nullptr for the specific name.
  auto saved_factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();

  // Find and remove the specific socket interface factory.
  auto& factories =
      Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories();
  auto it = factories.find("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
  if (it != factories.end()) {
    factories.erase(it);
  }

  // Create an address instance.
  UpstreamReverseConnectionAddress address("test-node");

  // The socketInterface() method should fall back to the default socket interface
  // when the upstream reverse connection socket interface is not found.
  const Network::SocketInterface& socket_interface = address.socketInterface();

  // Should return the default socket interface.
  EXPECT_NE(&socket_interface, nullptr);

  // Verify that it's not the reverse tunnel acceptor type.
  const auto* reverse_tunnel_acceptor =
      dynamic_cast<const BootstrapReverseConnection::ReverseTunnelAcceptor*>(&socket_interface);
  EXPECT_EQ(reverse_tunnel_acceptor, nullptr);

  // Explicitly verify that the returned interface is the one registered with
  // "envoy.extensions.network.socket_interface.default_socket_interface".
  const Network::SocketInterface* default_interface = Network::socketInterface(
      "envoy.extensions.network.socket_interface.default_socket_interface");
  EXPECT_NE(default_interface, nullptr);
  EXPECT_EQ(&socket_interface, default_interface);
  Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::factories() =
      saved_factories;
}

// Test logical name for multiple instances of UpstreamReverseConnectionAddress.
TEST_F(UpstreamReverseConnectionAddressTest, MultipleInstances) {
  UpstreamReverseConnectionAddress address1("node-1");
  UpstreamReverseConnectionAddress address2("node-2");

  // Test that different instances have different logical names.
  EXPECT_EQ(address1.logicalName(), "node-1");
  EXPECT_EQ(address2.logicalName(), "node-2");

  // Test that they are not equal.
  EXPECT_FALSE(address1 == address2);
}

TEST_F(UpstreamReverseConnectionAddressTest, EmptyNodeId) {
  UpstreamReverseConnectionAddress address("");

  // Test with empty node ID.
  EXPECT_EQ(address.logicalName(), "");
  EXPECT_EQ(address.asString(), "127.0.0.1:0");
  EXPECT_EQ(address.type(), Network::Address::Type::Ip);
}

TEST_F(UpstreamReverseConnectionAddressTest, LongNodeId) {
  const std::string long_node_id =
      "very-long-node-id-that-might-be-used-in-production-environments";
  UpstreamReverseConnectionAddress address(long_node_id);

  // Test with long node ID.
  EXPECT_EQ(address.logicalName(), long_node_id);
  EXPECT_EQ(address.asString(), "127.0.0.1:0");
  EXPECT_EQ(address.type(), Network::Address::Type::Ip);
}

// Test header-based formatter.
TEST_F(ReverseConnectionClusterTest, HeaderFormatterExpressions) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-node-id)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("production-node", "cluster-production");

  RevConCluster::LoadBalancer lb(cluster_);

  // Test header extraction.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-node-id", "production-node"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "production-node");
  }
}

// Test multiple header formatter combinations.
TEST_F(ReverseConnectionClusterTest, MultipleHeaderFormatters) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-env)%-%REQ(x-node-id)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("prod-node-123", "cluster-prod");
  addTestSocket("dev-node-456", "cluster-dev");

  RevConCluster::LoadBalancer lb(cluster_);

  // Test 1: Production environment with node-123.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-env", "prod"}, {"x-node-id", "node-123"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "prod-node-123");
  }

  // Test 2: Development environment with node-456.
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-env", "dev"}, {"x-node-id", "node-456"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "dev-node-456");
  }

  // Test 3: Missing header results in host creation (formatter returns "prod--" when x-node-id is
  // missing).
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-env", "prod"}}}; // Missing x-node-id

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr); // Host created for "prod--" (missing node-id becomes "-")
    EXPECT_EQ(result.host->address()->logicalName(), "prod--");
  }
}

// Test connection property formatters.
TEST_F(ReverseConnectionClusterTest, ConnectionPropertyFormatters) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "conn-%DOWNSTREAM_REMOTE_ADDRESS%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("conn-192.168.1.100:8080", "cluster-conn");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create stream info with connection info.
  auto stream_info = std::make_unique<NiceMock<StreamInfo::MockStreamInfo>>();
  auto remote = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.100", 8080);
  auto local = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0", 0);
  Network::ConnectionInfoSetterImpl conn_info(local, remote);
  ON_CALL(*stream_info, downstreamAddressProvider()).WillByDefault(ReturnRef(conn_info));

  NiceMock<Network::MockConnection> connection;
  ON_CALL(connection, streamInfo()).WillByDefault(testing::ReturnRef(*stream_info));
  TestLoadBalancerContext lb_context(&connection, stream_info.get());
  lb_context.downstream_headers_ =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-header", "value"}}};

  auto result = lb.chooseHost(&lb_context);
  ASSERT_NE(result.host, nullptr);
  EXPECT_EQ(result.host->address()->logicalName(), "conn-192.168.1.100:8080");
}

// Test formatter error handling and edge cases.
TEST_F(ReverseConnectionClusterTest, FormatterErrorHandling) {
  // Test missing header returns nullptr.
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-missing-header)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();

  RevConCluster::LoadBalancer lb(cluster_);
  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  lb_context.downstream_headers_ =
      Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-other-header", "value"}}};

  // Should return nullptr for missing header.
  auto result = lb.chooseHost(&lb_context);
  EXPECT_EQ(result.host, nullptr);
}

// Test formatter with complex combinations and transformations.
TEST_F(ReverseConnectionClusterTest, ComplexFormatterCombinations) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "svc-%REQ(x-service)%-env-%REQ(x-env)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("svc-api-env-prod", "cluster-complex");

  RevConCluster::LoadBalancer lb(cluster_);

  NiceMock<Network::MockConnection> connection;
  TestLoadBalancerContext lb_context(&connection);
  lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
      new Http::TestRequestHeaderMapImpl{{"x-service", "api"}, {"x-env", "prod"}}};

  auto result = lb.chooseHost(&lb_context);
  ASSERT_NE(result.host, nullptr);
  EXPECT_EQ(result.host->address()->logicalName(), "svc-api-env-prod");
}

// Test scalability with many different host IDs.
TEST_F(ReverseConnectionClusterTest, ScalabilityManyHostIds) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "node-%REQ(x-node-index)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();

  RevConCluster::LoadBalancer lb(cluster_);

  // Create sockets for 100 different nodes.
  for (int i = 0; i < 100; ++i) {
    std::string node_id = absl::StrCat("node-", i);
    addTestSocket(node_id, absl::StrCat("cluster-", i));
  }

  // Verify each node gets its own host.
  for (int i = 0; i < 100; ++i) {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    std::string node_index = absl::StrCat(i);
    lb_context.downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-node-index", node_index}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), absl::StrCat("node-", i));
  }
}

// Test formatter validation during cluster creation
TEST_F(ReverseConnectionClusterTest, FormatterValidationErrors) {
  // Test invalid format string.
  {
    const std::string yaml = R"EOF(
      name: name
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cleanup_interval: 1s
      cluster_type:
        name: envoy.clusters.reverse_connection
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
          cleanup_interval: 10s
          host_id_format: "%INVALID_COMMAND()%"
    )EOF";

    EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                              "Not supported field in StreamInfo: INVALID_COMMAND");
  }
}

// Test concurrent host creation and caching
TEST_F(ReverseConnectionClusterTest, ConcurrentHostCreation) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
        cleanup_interval: 10s
        host_id_format: "%REQ(x-concurrent-node)%"
  )EOF";

  setupFromYaml(yaml);
  setupUpstreamExtension();
  setupThreadLocalSlot();
  addTestSocket("concurrent-node-1", "cluster-concurrent");

  RevConCluster::LoadBalancer lb(cluster_);

  // Create multiple concurrent requests for the same node.
  std::vector<Upstream::HostConstSharedPtr> hosts;
  for (int i = 0; i < 10; ++i) {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-concurrent-node", "concurrent-node-1"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    hosts.push_back(result.host);
  }

  // All should return the same cached host.
  for (size_t i = 1; i < hosts.size(); ++i) {
    EXPECT_EQ(hosts[0], hosts[i]);
  }
}

// Test comprehensive formatter functionality with various format strings.
TEST_F(ReverseConnectionClusterTest, FormatterComprehensiveTests) {
  // Test 1: Simple header extraction.
  {
    const std::string yaml = R"EOF(
      name: name
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cleanup_interval: 1s
      cluster_type:
        name: envoy.clusters.reverse_connection
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
          cleanup_interval: 10s
          host_id_format: "%REQ(x-node-id)%"
    )EOF";

    setupFromYaml(yaml);
    setupUpstreamExtension();
    setupThreadLocalSlot();
    addTestSocket("node-123", "cluster-123");

    RevConCluster::LoadBalancer lb(cluster_);
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-node-id", "node-123"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "node-123");
  }

  // Test 2: Combined format with multiple headers.
  {
    const std::string yaml = R"EOF(
      name: name
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cleanup_interval: 1s
      cluster_type:
        name: envoy.clusters.reverse_connection
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
          cleanup_interval: 10s
          host_id_format: "%REQ(x-tenant)%-%REQ(x-region)%"
    )EOF";

    setupFromYaml(yaml);
    setupUpstreamExtension();
    setupThreadLocalSlot();
    addTestSocket("tenant-a-us-west", "cluster-multi");

    RevConCluster::LoadBalancer lb(cluster_);
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ = Http::RequestHeaderMapPtr{
        new Http::TestRequestHeaderMapImpl{{"x-tenant", "tenant-a"}, {"x-region", "us-west"}}};

    auto result = lb.chooseHost(&lb_context);
    ASSERT_NE(result.host, nullptr);
    EXPECT_EQ(result.host->address()->logicalName(), "tenant-a-us-west");
  }

  // Test 3: Missing header should result in no host.
  {
    const std::string yaml = R"EOF(
      name: name
      connect_timeout: 0.25s
      lb_policy: CLUSTER_PROVIDED
      cleanup_interval: 1s
      cluster_type:
        name: envoy.clusters.reverse_connection
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.ReverseConnectionClusterConfig
          cleanup_interval: 10s
          host_id_format: "%REQ(x-missing-header)%"
    )EOF";

    setupFromYaml(yaml);
    setupUpstreamExtension();
    setupThreadLocalSlot();

    RevConCluster::LoadBalancer lb(cluster_);
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection);
    lb_context.downstream_headers_ =
        Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{{"x-other-header", "value"}}};

    auto result = lb.chooseHost(&lb_context);
    EXPECT_EQ(result.host, nullptr);
  }
}

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
