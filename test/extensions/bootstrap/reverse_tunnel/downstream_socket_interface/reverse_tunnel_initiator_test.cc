#include <sys/socket.h>

#include "envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3/downstream_reverse_connection_socket_interface.pb.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_address.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_io_handle.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_tunnel_initiator.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/logging.h"

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

// ReverseTunnelInitiator Test Class.

class ReverseTunnelInitiatorTest : public testing::Test {
protected:
  ReverseTunnelInitiatorTest() {
    // Set up the stats scope.
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));

    // Set up the mock context.
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    EXPECT_CALL(context_, clusterManager()).WillRepeatedly(ReturnRef(cluster_manager_));

    // Create the socket interface.
    socket_interface_ = std::make_unique<ReverseTunnelInitiator>(context_);

    // Create the extension.
    extension_ = std::make_unique<ReverseTunnelInitiatorExtension>(context_, config_);
  }

  // Thread Local Setup Helpers.

  // Helper function to set up thread local slot for tests.
  void setupThreadLocalSlot() {
    // First, call onServerInitialized to set up the extension reference properly.
    extension_->onServerInitialized();

    // Create a thread local registry with the properly initialized extension.
    thread_local_registry_ =
        std::make_shared<DownstreamSocketThreadLocal>(dispatcher_, *stats_scope_);

    // Create the actual TypedSlot.
    tls_slot_ = ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);

    // Set up the slot to return our registry.
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&) { return registry; });

    // Override the TLS slot with our test version.
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));

    // Set the extension reference in the socket interface.
    socket_interface_->extension_ = extension_.get();
  }

  // Helper to create a test address.
  Network::Address::InstanceConstSharedPtr createTestAddress(const std::string& ip = "127.0.0.1",
                                                             uint32_t port = 8080) {
    return Network::Utility::parseInternetAddressNoThrow(ip, port);
  }

  void TearDown() override {
    tls_slot_.reset();
    thread_local_registry_.reset();
    extension_.reset();
    socket_interface_.reset();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_{"worker_0"};

  envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelInitiator> socket_interface_;
  std::unique_ptr<ReverseTunnelInitiatorExtension> extension_;

  // Real thread local slot and registry.
  std::unique_ptr<ThreadLocal::TypedSlot<DownstreamSocketThreadLocal>> tls_slot_;
  std::shared_ptr<DownstreamSocketThreadLocal> thread_local_registry_;

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

TEST_F(ReverseTunnelInitiatorTest, CreateBootstrapExtension) {
  // Test createBootstrapExtension function.
  envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface config;

  auto extension = socket_interface_->createBootstrapExtension(config, context_);
  EXPECT_NE(extension, nullptr);

  // Verify extension is stored in socket interface.
  EXPECT_NE(socket_interface_->getExtension(), nullptr);
}

TEST_F(ReverseTunnelInitiatorTest, CreateEmptyConfigProto) {
  // Test createEmptyConfigProto function.
  auto config = socket_interface_->createEmptyConfigProto();
  EXPECT_NE(config, nullptr);

  // Should be able to cast to the correct type.
  auto* typed_config =
      dynamic_cast<envoy::extensions::bootstrap::reverse_tunnel::downstream_socket_interface::v3::
                       DownstreamReverseConnectionSocketInterface*>(config.get());
  EXPECT_NE(typed_config, nullptr);
}

TEST_F(ReverseTunnelInitiatorTest, IpFamilySupported) {
  // Test IP family support.
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET));
  EXPECT_TRUE(socket_interface_->ipFamilySupported(AF_INET6));
  EXPECT_FALSE(socket_interface_->ipFamilySupported(AF_UNIX));
}

TEST_F(ReverseTunnelInitiatorTest, GetLocalRegistryNoExtension) {
  // Test getLocalRegistry when extension is not set.
  auto* registry = socket_interface_->getLocalRegistry();
  EXPECT_EQ(registry, nullptr);
}

TEST_F(ReverseTunnelInitiatorTest, GetLocalRegistryWithExtension) {
  // Test getLocalRegistry when extension is set.
  setupThreadLocalSlot();

  auto* registry = socket_interface_->getLocalRegistry();
  EXPECT_NE(registry, nullptr);
  EXPECT_EQ(registry, thread_local_registry_.get());
}

TEST_F(ReverseTunnelInitiatorTest, FactoryName) {
  EXPECT_EQ(socket_interface_->name(),
            "envoy.bootstrap.reverse_tunnel.downstream_socket_interface");
}

TEST_F(ReverseTunnelInitiatorTest, SocketMethodBasicIPv4) {
  // Test basic socket creation for IPv4.
  auto socket = socket_interface_->socket(Network::Socket::Type::Stream, Network::Address::Type::Ip,
                                          Network::Address::IpVersion::v4, false,
                                          Network::SocketCreationOptions{});
  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());

  // Verify it's NOT a ReverseConnectionIOHandle (should be a regular socket)
  auto* reverse_handle = dynamic_cast<ReverseConnectionIOHandle*>(socket.get());
  EXPECT_EQ(reverse_handle, nullptr);
}

TEST_F(ReverseTunnelInitiatorTest, SocketMethodBasicIPv6) {
  // Test basic socket creation for IPv6.
  auto socket = socket_interface_->socket(Network::Socket::Type::Stream, Network::Address::Type::Ip,
                                          Network::Address::IpVersion::v6, false,
                                          Network::SocketCreationOptions{});
  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());
}

TEST_F(ReverseTunnelInitiatorTest, SocketMethodDatagram) {
  // Test datagram socket creation.
  auto socket = socket_interface_->socket(
      Network::Socket::Type::Datagram, Network::Address::Type::Ip, Network::Address::IpVersion::v4,
      false, Network::SocketCreationOptions{});
  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());
}

TEST_F(ReverseTunnelInitiatorTest, SocketMethodUnixDomain) {
  // Test Unix domain socket creation.
  auto socket = socket_interface_->socket(
      Network::Socket::Type::Stream, Network::Address::Type::Pipe, Network::Address::IpVersion::v4,
      false, Network::SocketCreationOptions{});
  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());
}

TEST_F(ReverseTunnelInitiatorTest, SocketMethodWithAddressIPv4) {
  // Test socket creation with IPv4 address.
  auto address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080);
  auto socket = socket_interface_->socket(Network::Socket::Type::Stream, address,
                                          Network::SocketCreationOptions{});
  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());
}

TEST_F(ReverseTunnelInitiatorTest, SocketMethodWithAddressIPv6) {
  // Test socket creation with IPv6 address.
  auto address = std::make_shared<Network::Address::Ipv6Instance>("::1", 8080);
  auto socket = socket_interface_->socket(Network::Socket::Type::Stream, address,
                                          Network::SocketCreationOptions{});
  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());
}

TEST_F(ReverseTunnelInitiatorTest, SocketMethodWithReverseConnectionAddress) {
  // Test socket creation with ReverseConnectionAddress.
  ReverseConnectionAddress::ReverseConnectionConfig config;
  config.src_cluster_id = "test-cluster";
  config.src_node_id = "test-node";
  config.src_tenant_id = "test-tenant";
  config.remote_cluster = "remote-cluster";
  config.connection_count = 2;

  auto reverse_address = std::make_shared<ReverseConnectionAddress>(config);

  auto socket = socket_interface_->socket(Network::Socket::Type::Stream, reverse_address,
                                          Network::SocketCreationOptions{});
  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());

  // Verify it's a ReverseConnectionIOHandle (not a regular socket)
  auto* reverse_handle = dynamic_cast<ReverseConnectionIOHandle*>(socket.get());
  EXPECT_NE(reverse_handle, nullptr);
}

TEST_F(ReverseTunnelInitiatorTest, CreateReverseConnectionSocketStreamIPv4) {
  // Test createReverseConnectionSocket for stream IPv4 with TLS registry setup.
  setupThreadLocalSlot();

  ReverseConnectionSocketConfig config;
  config.src_cluster_id = "test-cluster";
  config.src_node_id = "test-node";
  config.src_tenant_id = "test-tenant";
  config.remote_clusters.push_back(RemoteClusterConnectionConfig("remote-cluster", 2));

  auto socket = socket_interface_->createReverseConnectionSocket(
      Network::Socket::Type::Stream, Network::Address::Type::Ip, Network::Address::IpVersion::v4,
      config);

  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());

  // Verify it's a ReverseConnectionIOHandle.
  auto* reverse_handle = dynamic_cast<ReverseConnectionIOHandle*>(socket.get());
  EXPECT_NE(reverse_handle, nullptr);

  // Verify that the TLS registry scope is being used.
  // The socket should be created with the scope from TLS registry, not context scope.
  EXPECT_EQ(&reverse_handle->getDownstreamExtension()->getStatsScope(), stats_scope_.get());
}

TEST_F(ReverseTunnelInitiatorTest, CreateReverseConnectionSocketStreamIPv6) {
  // Test createReverseConnectionSocket for stream IPv6.
  ReverseConnectionSocketConfig config;
  config.src_cluster_id = "test-cluster";
  config.src_node_id = "test-node";
  config.src_tenant_id = "test-tenant";
  config.remote_clusters.push_back(RemoteClusterConnectionConfig("remote-cluster", 2));

  auto socket = socket_interface_->createReverseConnectionSocket(
      Network::Socket::Type::Stream, Network::Address::Type::Ip, Network::Address::IpVersion::v6,
      config);

  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());

  // Verify it's a ReverseConnectionIOHandle.
  auto* reverse_handle = dynamic_cast<ReverseConnectionIOHandle*>(socket.get());
  EXPECT_NE(reverse_handle, nullptr);
}

TEST_F(ReverseTunnelInitiatorTest, CreateReverseConnectionSocketDatagram) {
  // Test createReverseConnectionSocket for datagram (should fallback to regular socket)
  ReverseConnectionSocketConfig config;
  config.src_cluster_id = "test-cluster";
  config.src_node_id = "test-node";
  config.src_tenant_id = "test-tenant";
  config.remote_clusters.push_back(RemoteClusterConnectionConfig("remote-cluster", 2));

  auto socket = socket_interface_->createReverseConnectionSocket(
      Network::Socket::Type::Datagram, Network::Address::Type::Ip, Network::Address::IpVersion::v4,
      config);

  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());

  // Verify it's NOT a ReverseConnectionIOHandle.
  auto* reverse_handle = dynamic_cast<ReverseConnectionIOHandle*>(socket.get());
  EXPECT_EQ(reverse_handle, nullptr);
}

TEST_F(ReverseTunnelInitiatorTest, CreateReverseConnectionSocketNonIP) {
  // Test createReverseConnectionSocket for non-IP address (should fallback to regular socket)
  ReverseConnectionSocketConfig config;
  config.src_cluster_id = "test-cluster";
  config.src_node_id = "test-node";
  config.src_tenant_id = "test-tenant";
  config.remote_clusters.push_back(RemoteClusterConnectionConfig("remote-cluster", 2));

  auto socket = socket_interface_->createReverseConnectionSocket(
      Network::Socket::Type::Stream, Network::Address::Type::Pipe, Network::Address::IpVersion::v4,
      config);

  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());

  // Verify it's NOT a ReverseConnectionIOHandle (should be a regular socket)
  auto* reverse_handle = dynamic_cast<ReverseConnectionIOHandle*>(socket.get());
  EXPECT_EQ(reverse_handle, nullptr);
}

TEST_F(ReverseTunnelInitiatorTest, CreateReverseConnectionSocketEmptyRemoteClusters) {
  // Test createReverseConnectionSocket with empty remote_clusters (should return early)
  ReverseConnectionSocketConfig config;
  config.src_cluster_id = "test-cluster";
  config.src_node_id = "test-node";
  config.src_tenant_id = "test-tenant";
  // No remote_clusters added - should return early.

  auto socket = socket_interface_->createReverseConnectionSocket(
      Network::Socket::Type::Stream, Network::Address::Type::Ip, Network::Address::IpVersion::v4,
      config);

  // Should return nullptr due to empty remote_clusters.
  EXPECT_EQ(socket, nullptr);
}

TEST_F(ReverseTunnelInitiatorTest, SocketMethodWithEmptyReverseConnectionAddress) {
  // Test socket creation with empty ReverseConnectionAddress.
  ReverseConnectionAddress::ReverseConnectionConfig config;
  config.src_cluster_id = "";
  config.src_node_id = "";
  config.src_tenant_id = "";
  config.remote_cluster = "";
  config.connection_count = 0;

  auto reverse_address = std::make_shared<ReverseConnectionAddress>(config);

  auto socket = socket_interface_->socket(Network::Socket::Type::Stream, reverse_address,
                                          Network::SocketCreationOptions{});
  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());
}

TEST_F(ReverseTunnelInitiatorTest, SocketMethodWithSocketCreationOptions) {
  // Test socket creation with socket creation options.
  Network::SocketCreationOptions options;
  options.mptcp_enabled_ = true;
  options.max_addresses_cache_size_ = 100;

  auto address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  auto socket = socket_interface_->socket(Network::Socket::Type::Stream, address, options);
  EXPECT_NE(socket, nullptr);
  EXPECT_TRUE(socket->isOpen());
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
