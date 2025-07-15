#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/reverse_connection/v3/reverse_connection.pb.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/http/headers.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/singleton/manager_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/clusters/reverse_connection/reverse_connection.h"
#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_acceptor.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"
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
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

// Test socket manager that provides predictable getNodeID behavior
class TestUpstreamSocketManager : public BootstrapReverseConnection::UpstreamSocketManager {
public:
  TestUpstreamSocketManager(Event::Dispatcher& dispatcher, Stats::Scope& scope)
      : BootstrapReverseConnection::UpstreamSocketManager(dispatcher, scope, nullptr) {
    std::cout << "TestUpstreamSocketManager: Constructor called" << std::endl;
  }
  
  // This hides the base class's getNodeID method 
  std::string getNodeID(const std::string& key) {
    std::cout << "TestUpstreamSocketManager::getNodeID() called with key: " << key << std::endl;
    std::string result = "test-node-" + key;
    std::cout << "TestUpstreamSocketManager::getNodeID() returning: " << result << std::endl;
    return result;
  }
};

// Test thread local registry that provides our test socket manager
class TestUpstreamSocketThreadLocal : public BootstrapReverseConnection::UpstreamSocketThreadLocal {
public:
  TestUpstreamSocketThreadLocal(Event::Dispatcher& dispatcher, Stats::Scope& scope)
      : BootstrapReverseConnection::UpstreamSocketThreadLocal(dispatcher, scope, nullptr),
        test_socket_manager_(dispatcher, scope) {
    std::cout << "TestUpstreamSocketThreadLocal: Constructor called" << std::endl;
  }
  
  // Override both const and non-const versions of socketManager
  BootstrapReverseConnection::UpstreamSocketManager* socketManager() {
    std::cout << "TestUpstreamSocketThreadLocal::socketManager() (non-const) called" << std::endl;
    std::cout << "TestUpstreamSocketThreadLocal::socketManager() returning: " << &test_socket_manager_ << std::endl;
    return &test_socket_manager_;
  }
  
  const BootstrapReverseConnection::UpstreamSocketManager* socketManager() const {
    std::cout << "TestUpstreamSocketThreadLocal::socketManager() (const) called" << std::endl;
    std::cout << "TestUpstreamSocketThreadLocal::socketManager() returning: " << &test_socket_manager_ << std::endl;
    return &test_socket_manager_;
  }
  
private:
  TestUpstreamSocketManager test_socket_manager_;
};

// Forward declaration
class TestReverseTunnelAcceptor;

// Simple test extension that just returns our registry
class SimpleTestExtension {
public:
  SimpleTestExtension(TestUpstreamSocketThreadLocal& registry) : test_registry_(registry) {}
  
  BootstrapReverseConnection::UpstreamSocketThreadLocal* getLocalRegistry() const {
    std::cout << "SimpleTestExtension::getLocalRegistry() called" << std::endl;
    return &test_registry_;
  }
  
private:
  TestUpstreamSocketThreadLocal& test_registry_;
};

// Test reverse tunnel acceptor that returns our test registry
class TestReverseTunnelAcceptor : public BootstrapReverseConnection::ReverseTunnelAcceptor {
public:
  TestReverseTunnelAcceptor(Server::Configuration::ServerFactoryContext& context)
      : BootstrapReverseConnection::ReverseTunnelAcceptor(context),
        test_registry_(context.mainThreadDispatcher(), context.scope()),
        simple_extension_(test_registry_) {
    std::cout << "TestReverseTunnelAcceptor: Constructor called" << std::endl;
    
    // This is a hack: we'll reinterpret_cast our simple extension to fool the type system
    // This is unsafe but should work for testing since we only call getLocalRegistry()
    extension_ = reinterpret_cast<BootstrapReverseConnection::ReverseTunnelAcceptorExtension*>(&simple_extension_);
    std::cout << "TestReverseTunnelAcceptor: extension_ set to: " << extension_ << std::endl;
  }
  
  // Override the name to ensure it matches what the test expects
  std::string name() const override {
    return "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface";
  }
  
private:
  mutable TestUpstreamSocketThreadLocal test_registry_;
  SimpleTestExtension simple_extension_;
};

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

  // Upstream::LoadBalancerContext
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
    // // Create our test acceptor FIRST
    // test_acceptor_ = std::make_unique<TestReverseTunnelAcceptor>(server_context_);
    
    // // Inject our test acceptor as a BootstrapExtensionFactory (which is what socketInterface() looks for)
    // factory_injection_ = std::make_unique<Registry::InjectFactory<Server::Configuration::BootstrapExtensionFactory>>(*test_acceptor_);
    
    // // Print all registered factories for debugging AFTER injection
    // printRegisteredFactories();
  }
  
  ~ReverseConnectionClusterTest() override = default;

  void printRegisteredFactories() {
    std::cout << "=== Registered Bootstrap Extension Factories ===" << std::endl;
    for (const auto& ext : Envoy::Registry::FactoryCategoryRegistry::registeredFactories()) {
      if (ext.first == "envoy.bootstrap") {
        std::cout << "Category: " << ext.first << std::endl;
        for (const auto& name : ext.second->registeredNames()) {
          std::cout << "  - " << name << std::endl;
        }
      }
    }
    
    std::cout << "=== Registered Socket Interface Factories ===" << std::endl;
    auto& socket_factories = Registry::FactoryRegistry<Network::SocketInterface>::factories();
    for (const auto& [name, factory] : socket_factories) {
      std::cout << "  - " << name << " (ptr: " << factory << ")" << std::endl;
    }
    
    std::cout << "=== Testing socketInterface lookup ===" << std::endl;
    
    // Check what's in the BootstrapExtensionFactory registry
    std::cout << "Checking BootstrapExtensionFactory registry:" << std::endl;
    auto* factory = Registry::FactoryRegistry<Server::Configuration::BootstrapExtensionFactory>::getFactory(
        "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");
    std::cout << "Factory from registry: " << factory << std::endl;
    std::cout << "Our test acceptor: " << test_acceptor_.get() << std::endl;
    
    auto* found = Network::socketInterface("envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");
    std::cout << "Found socket interface: " << (found ? "YES" : "NO") << std::endl;
    if (found) {
      std::cout << "Socket interface ptr: " << found << std::endl;
      std::cout << "Our test acceptor ptr: " << test_acceptor_.get() << std::endl;
      
      // Test the dynamic_cast
      auto* cast_result = dynamic_cast<const BootstrapReverseConnection::ReverseTunnelAcceptor*>(found);
      std::cout << "Dynamic cast result: " << cast_result << std::endl;
      if (cast_result) {
        std::cout << "Cast succeeded, calling getLocalRegistry()" << std::endl;
        auto* registry = cast_result->getLocalRegistry();
        std::cout << "getLocalRegistry() returned: " << registry << std::endl;
      } else {
        std::cout << "Cast failed!" << std::endl;
      }
    }
  }

  void setupFromYaml(const std::string& yaml, bool expect_success = true) {
    if (expect_success) {
      cleanup_timer_ = new Event::MockTimer(&server_context_.dispatcher_);
      EXPECT_CALL(*cleanup_timer_, enableTimer(_, _));
    }
    setup(Upstream::parseClusterFromV3Yaml(yaml));
  }

  void setup(const envoy::config::cluster::v3::Cluster& cluster_config) {

    Envoy::Upstream::ClusterFactoryContextImpl factory_context(server_context_, nullptr, nullptr,
                                                               false);

    RevConClusterFactory factory;
    
    // Parse the RevConClusterConfig from the cluster's typed_config
    envoy::extensions::clusters::reverse_connection::v3::RevConClusterConfig rev_con_config;
    THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
        cluster_config.cluster_type().typed_config(), 
        validation_visitor_, 
        rev_con_config));
    
    auto status_or_pair = factory.createClusterWithConfig(cluster_config, rev_con_config, factory_context);
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
    if (init_complete_) {
      // EXPECT_CALL(server_context_.dispatcher_, post(_));
      EXPECT_CALL(*cleanup_timer_, disableTimer());
    }
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  Stats::TestUtil::TestStore& stats_store_ = server_context_.store_;

  std::shared_ptr<RevConCluster> cluster_;
  ReadyWatcher membership_updated_;
  ReadyWatcher initialized_;
  Event::MockTimer* cleanup_timer_;
  Common::CallbackHandlePtr priority_update_cb_;
  bool init_complete_{false};
  
  // Test factory injection
  std::unique_ptr<TestReverseTunnelAcceptor> test_acceptor_;
  std::unique_ptr<Registry::InjectFactory<Server::Configuration::BootstrapExtensionFactory>> factory_injection_;
};

namespace {

TEST(ReverseConnectionClusterConfigTest, GoodConfig) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
        http_header_names:
          - x-remote-node-id
          - x-dst-cluster-uuid
  )EOF";

  envoy::config::cluster::v3::Cluster cluster_config = Upstream::parseClusterFromV3Yaml(yaml);
  EXPECT_TRUE(cluster_config.has_cluster_type());
  EXPECT_EQ(cluster_config.cluster_type().name(), "envoy.clusters.reverse_connection");
}

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
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                            "Reverse Conn clusters must have no load assignment configured");
}

TEST_F(ReverseConnectionClusterTest, BadConfigWithWrongLbPolicy) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: ROUND_ROBIN
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(setupFromYaml(yaml, false), EnvoyException,
                            "cluster: LB policy ROUND_ROBIN is not valid for Cluster type envoy.clusters.reverse_connection. Only 'CLUSTER_PROVIDED' is allowed with cluster type 'REVERSE_CONNECTION'");
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
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
        http_header_names:
          - x-remote-node-id
          - x-dst-cluster-uuid
  )EOF";

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(membership_updated_, ready()).Times(0);
  setupFromYaml(yaml);

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
}

TEST_F(ReverseConnectionClusterTest, NoContext) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
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
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }
}

TEST_F(ReverseConnectionClusterTest, NoHeaders) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
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

TEST_F(ReverseConnectionClusterTest, MissingRequiredHeaders) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  // Request with unsupported headers but missing all required headers (EnvoyDstNodeUUID, EnvoyDstClusterUUID, proper Host header)
  {
    NiceMock<Network::MockConnection> connection;
    TestLoadBalancerContext lb_context(&connection, "x-random-header", "random-value");
    RevConCluster::LoadBalancer lb(cluster_);
    EXPECT_CALL(server_context_.dispatcher_, post(_)).Times(0);
    Upstream::HostConstSharedPtr host = lb.chooseHost(&lb_context).host;
    EXPECT_EQ(host, nullptr);
  }
}

TEST_F(ReverseConnectionClusterTest, GetUUIDFromHostFunction) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  RevConCluster::LoadBalancer lb(cluster_);

  // Test valid Host header format
  {
    auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"Host", "test-node-uuid.tcpproxy.envoy.remote:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test-node-uuid");
  }

  // Test valid Host header format with different UUID
  {
    auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"Host", "another-test-node-uuid.tcpproxy.envoy.remote:9090"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "another-test-node-uuid");
  }

  // Test Host header without port
  {
    auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"Host", "test-node-uuid.tcpproxy.envoy.remote"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test-node-uuid");
  }

  // Test invalid Host header - wrong suffix
  {
    auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"Host", "test-node-uuid.wrong.suffix:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_FALSE(result.has_value());
  }

  // Test invalid Host header - no dot separator
  {
    auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"Host", "test-node-uuidtcpproxy.envoy.remote:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_FALSE(result.has_value());
  }

  // Test invalid Host header - empty UUID
  {
    auto headers = Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
        {"Host", ".tcpproxy.envoy.remote:8080"}}};
    auto result = lb.getUUIDFromHost(*headers);
    EXPECT_EQ(result.value(), "");
  }
}

TEST_F(ReverseConnectionClusterTest, GetUUIDFromSNIFunction) {
  const std::string yaml = R"EOF(
    name: name
    connect_timeout: 0.25s
    lb_policy: CLUSTER_PROVIDED
    cleanup_interval: 1s
    cluster_type:
      name: envoy.clusters.reverse_connection
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
        cleanup_interval: 10s
  )EOF";

  EXPECT_CALL(initialized_, ready());
  setupFromYaml(yaml);

  RevConCluster::LoadBalancer lb(cluster_);

  // Test valid SNI format
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return("test-node-uuid.tcpproxy.envoy.remote"));
    
    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "test-node-uuid");
  }

  // Test valid SNI format with different UUID
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return("another-test-node123.tcpproxy.envoy.remote"));
    
    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "another-test-node123");
  }

  // Test empty SNI
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return(""));
    
    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_FALSE(result.has_value());
  }

  // Test null connection
  {
    auto result = lb.getUUIDFromSNI(nullptr);
    EXPECT_FALSE(result.has_value());
  }

  // Test SNI with wrong suffix
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return("test-node-uuid.wrong.suffix"));
    
    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_FALSE(result.has_value());
  }

  // Test SNI without suffix
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return("test-node-uuid"));
    
    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_FALSE(result.has_value());
  }

  // Test SNI with empty UUID
  {
    NiceMock<Network::MockConnection> connection;
    EXPECT_CALL(connection, requestedServerName())
        .WillRepeatedly(Return(".tcpproxy.envoy.remote"));
    
    auto result = lb.getUUIDFromSNI(&connection);
    EXPECT_EQ(result.value(), "");
  }
}

// TEST_F(ReverseConnectionClusterTest, HostCreationWithSocketManager) {
//   const std::string yaml = R"EOF(
//     name: name
//     connect_timeout: 0.25s
//     lb_policy: CLUSTER_PROVIDED
//     cleanup_interval: 1s
//     cluster_type:
//       name: envoy.clusters.reverse_connection
//       typed_config:
//         "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
//         cleanup_interval: 10s
//   )EOF";

//   EXPECT_CALL(initialized_, ready());
//   setupFromYaml(yaml);

//   RevConCluster::LoadBalancer lb(cluster_);

//   // Test host creation with Host header
//   {
//     NiceMock<Network::MockConnection> connection;
//     TestLoadBalancerContext lb_context(&connection);
//     lb_context.downstream_headers_ = 
//         Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
//             {"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};
    
//     auto result = lb.chooseHost(&lb_context);
//     EXPECT_NE(result.host, nullptr);
//     EXPECT_EQ(result.host->address()->logicalName(), "test-node-test-uuid-123");
//   }

//   // Test host creation with SNI
//   {
//     NiceMock<Network::MockConnection> connection;
//     EXPECT_CALL(connection, requestedServerName())
//         .WillRepeatedly(Return("test-uuid-456.tcpproxy.envoy.remote"));
    
//     TestLoadBalancerContext lb_context(&connection);
//     // No Host header, so it should fall back to SNI
//     lb_context.downstream_headers_ = 
//         Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{}};
    
//     auto result = lb.chooseHost(&lb_context);
//     EXPECT_NE(result.host, nullptr);
//     EXPECT_EQ(result.host->address()->logicalName(), "test-node-test-uuid-456");
//   }

//   // Test host creation with HTTP headers
//   {
//     NiceMock<Network::MockConnection> connection;
//     TestLoadBalancerContext lb_context(&connection, "x-dst-cluster-uuid", "cluster-123");
    
//     auto result = lb.chooseHost(&lb_context);
//     EXPECT_NE(result.host, nullptr);
//     EXPECT_EQ(result.host->address()->logicalName(), "test-node-cluster-123");
//   }
// }

// TEST_F(ReverseConnectionClusterTest, HostReuse) {
//   const std::string yaml = R"EOF(
//     name: name
//     connect_timeout: 0.25s
//     lb_policy: CLUSTER_PROVIDED
//     cleanup_interval: 1s
//     cluster_type:
//       name: envoy.clusters.reverse_connection
//       typed_config:
//         "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
//         cleanup_interval: 10s
//   )EOF";

//   EXPECT_CALL(initialized_, ready());
//   setupFromYaml(yaml);

//   RevConCluster::LoadBalancer lb(cluster_);

//   // Create first host
//   {
//     NiceMock<Network::MockConnection> connection;
//     TestLoadBalancerContext lb_context(&connection);
//     lb_context.downstream_headers_ = 
//         Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
//             {"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};
    
//     auto result1 = lb.chooseHost(&lb_context);
//     EXPECT_NE(result1.host, nullptr);
    
//     // Create second host with same UUID - should reuse the same host
//     auto result2 = lb.chooseHost(&lb_context);
//     EXPECT_NE(result2.host, nullptr);
//     EXPECT_EQ(result1.host, result2.host);
//   }
// }

// TEST_F(ReverseConnectionClusterTest, DifferentHostsForDifferentUUIDs) {
//   const std::string yaml = R"EOF(
//     name: name
//     connect_timeout: 0.25s
//     lb_policy: CLUSTER_PROVIDED
//     cleanup_interval: 1s
//     cluster_type:
//       name: envoy.clusters.reverse_connection
//       typed_config:
//         "@type": type.googleapis.com/envoy.extensions.clusters.reverse_connection.v3.RevConClusterConfig
//         cleanup_interval: 10s
//   )EOF";

//   EXPECT_CALL(initialized_, ready());
//   setupFromYaml(yaml);

//   RevConCluster::LoadBalancer lb(cluster_);

//   // Create first host
//   {
//     NiceMock<Network::MockConnection> connection;
//     TestLoadBalancerContext lb_context(&connection);
//     lb_context.downstream_headers_ = 
//         Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
//             {"Host", "test-uuid-123.tcpproxy.envoy.remote:8080"}}};
    
//     auto result1 = lb.chooseHost(&lb_context);
//     EXPECT_NE(result1.host, nullptr);
    
//     // Create second host with different UUID - should be different host
//     lb_context.downstream_headers_ = 
//         Http::RequestHeaderMapPtr{new Http::TestRequestHeaderMapImpl{
//             {"Host", "test-uuid-456.tcpproxy.envoy.remote:8080"}}};
//     auto result2 = lb.chooseHost(&lb_context);
//     EXPECT_NE(result2.host, nullptr);
//     EXPECT_NE(result1.host, result2.host);
//   }
// }

} // namespace
} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy 