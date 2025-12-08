#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/singleton/threadsafe_singleton.h"
#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_address.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/registry.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionAddressTest : public testing::Test {
protected:
  void SetUp() override {}

  // Helper function to create a test config.
  ReverseConnectionAddress::ReverseConnectionConfig createTestConfig() {
    return ReverseConnectionAddress::ReverseConnectionConfig{
        "test-node-123", "test-cluster-456", "test-tenant-789", "remote-cluster-abc", 5};
  }

  // Helper function to create a test address.
  ReverseConnectionAddress createTestAddress() {
    return ReverseConnectionAddress(createTestConfig());
  }

  // Set log level to debug for this test class.
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

// Test constructor and basic properties.
TEST_F(ReverseConnectionAddressTest, BasicSetup) {
  auto config = createTestConfig();
  ReverseConnectionAddress address(config);

  // Test that the address string is set correctly.
  EXPECT_EQ(address.asString(), "127.0.0.1:0");
  EXPECT_EQ(address.asStringView(), "127.0.0.1:0");

  // Test that the logical name is formatted correctly.
  std::string expected_logical_name =
      "rc://test-node-123:test-cluster-456:test-tenant-789@remote-cluster-abc:5";
  EXPECT_EQ(address.logicalName(), expected_logical_name);

  // Test address type.
  EXPECT_EQ(address.type(), Network::Address::Type::Ip);
  EXPECT_EQ(address.addressType(), "reverse_connection");
}

// Test equality operator.
TEST_F(ReverseConnectionAddressTest, EqualityOperator) {
  auto config1 = createTestConfig();
  auto config2 = createTestConfig();

  ReverseConnectionAddress address1(config1);
  ReverseConnectionAddress address2(config2);

  // Same config should be equal.
  EXPECT_TRUE(address1 == address2);
  EXPECT_TRUE(address2 == address1);

  // Different configs should not be equal.
  config2.src_node_id = "different-node";
  ReverseConnectionAddress address3(config2);
  EXPECT_FALSE(address1 == address3);
  EXPECT_FALSE(address3 == address1);
}

// Test equality with different address types.
TEST_F(ReverseConnectionAddressTest, EqualityWithDifferentTypes) {
  auto config = createTestConfig();
  ReverseConnectionAddress address(config);

  // Create a regular IPv4 address.
  auto regular_address = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 8080);

  // Should not be equal to different address types.
  EXPECT_FALSE(address == *regular_address);
  EXPECT_FALSE(*regular_address == address);
}

// Test reverse connection config accessor.
TEST_F(ReverseConnectionAddressTest, ReverseConnectionConfig) {
  auto config = createTestConfig();
  ReverseConnectionAddress address(config);

  const auto& retrieved_config = address.reverseConnectionConfig();

  EXPECT_EQ(retrieved_config.src_node_id, config.src_node_id);
  EXPECT_EQ(retrieved_config.src_cluster_id, config.src_cluster_id);
  EXPECT_EQ(retrieved_config.src_tenant_id, config.src_tenant_id);
  EXPECT_EQ(retrieved_config.remote_cluster, config.remote_cluster);
  EXPECT_EQ(retrieved_config.connection_count, config.connection_count);
}

// Test IP address properties.
TEST_F(ReverseConnectionAddressTest, IpAddressProperties) {
  auto config = createTestConfig();
  ReverseConnectionAddress address(config);

  // Should have IP address.
  EXPECT_NE(address.ip(), nullptr);
  EXPECT_EQ(address.ip()->addressAsString(), "127.0.0.1");
  EXPECT_EQ(address.ip()->port(), 0);

  // Should not have pipe or envoy internal address.
  EXPECT_EQ(address.pipe(), nullptr);
  EXPECT_EQ(address.envoyInternalAddress(), nullptr);
}

// Test socket address properties.
TEST_F(ReverseConnectionAddressTest, SocketAddressProperties) {
  auto config = createTestConfig();
  ReverseConnectionAddress address(config);

  const sockaddr* sock_addr = address.sockAddr();
  EXPECT_NE(sock_addr, nullptr);

  socklen_t addr_len = address.sockAddrLen();
  EXPECT_EQ(addr_len, sizeof(struct sockaddr_in));

  // Verify the sockaddr structure.
  const struct sockaddr_in* addr_in = reinterpret_cast<const struct sockaddr_in*>(sock_addr);
  EXPECT_EQ(addr_in->sin_family, AF_INET);
  EXPECT_EQ(addr_in->sin_port, htons(0));                      // Port 0
  EXPECT_EQ(addr_in->sin_addr.s_addr, htonl(INADDR_LOOPBACK)); // 127.0.0.1
}

// Test network namespace.
TEST_F(ReverseConnectionAddressTest, NetworkNamespace) {
  auto config = createTestConfig();
  ReverseConnectionAddress address(config);

  // Should not have a network namespace.
  auto namespace_opt = address.networkNamespace();
  EXPECT_FALSE(namespace_opt.has_value());
  EXPECT_EQ(nullptr, address.withNetworkNamespace("/var/run/netns/1"));
}

// Test socket interface.
TEST_F(ReverseConnectionAddressTest, SocketInterface) {
  auto config = createTestConfig();
  ReverseConnectionAddress address(config);

  // Should return the default socket interface.
  const auto& socket_interface = address.socketInterface();
  EXPECT_NE(&socket_interface, nullptr);
}

// Test socket interface with registered reverse connection interface.
TEST_F(ReverseConnectionAddressTest, SocketInterfaceWithReverseInterface) {
  // Create a mock socket interface that extends SocketInterfaceBase and registers itself
  class TestReverseSocketInterface : public Network::SocketInterfaceBase {
  public:
    TestReverseSocketInterface() = default;

    // Network::SocketInterface
    Network::IoHandlePtr socket(Network::Socket::Type socket_type, Network::Address::Type addr_type,
                                Network::Address::IpVersion version, bool socket_v6only,
                                const Network::SocketCreationOptions& options) const override {
      UNREFERENCED_PARAMETER(socket_v6only);
      UNREFERENCED_PARAMETER(options);
      // Create a regular socket for testing
      if (socket_type == Network::Socket::Type::Stream && addr_type == Network::Address::Type::Ip) {
        int domain = (version == Network::Address::IpVersion::v4) ? AF_INET : AF_INET6;
        int sock_fd = ::socket(domain, SOCK_STREAM, 0);
        if (sock_fd == -1) {
          return nullptr;
        }
        return std::make_unique<Network::IoSocketHandleImpl>(sock_fd);
      }
      return nullptr;
    }

    Network::IoHandlePtr socket(Network::Socket::Type socket_type,
                                const Network::Address::InstanceConstSharedPtr addr,
                                const Network::SocketCreationOptions& options) const override {
      // Delegate to the other socket method
      return socket(socket_type, addr->type(),
                    addr->ip() ? addr->ip()->version() : Network::Address::IpVersion::v4, false,
                    options);
    }

    bool ipFamilySupported(int domain) override { return domain == AF_INET || domain == AF_INET6; }

    // Server::Configuration::BootstrapExtensionFactory
    Server::BootstrapExtensionPtr
    createBootstrapExtension(const Protobuf::Message& config,
                             Server::Configuration::ServerFactoryContext& context) override {
      UNREFERENCED_PARAMETER(config);
      UNREFERENCED_PARAMETER(context);
      return nullptr;
    }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override { return nullptr; }

    std::string name() const override {
      return "envoy.bootstrap.reverse_tunnel.downstream_socket_interface";
    }

    std::set<std::string> configTypes() override { return {}; }
  };

  // Register the test interface in the registry
  TestReverseSocketInterface test_interface;
  Registry::InjectFactory<Server::Configuration::BootstrapExtensionFactory> registered_factory(
      test_interface);

  auto config = createTestConfig();
  ReverseConnectionAddress address(config);

  // Should return the registered test socket interface.
  const auto& socket_interface = address.socketInterface();
  EXPECT_EQ(&socket_interface, &test_interface);
}

// Test with empty configuration values.
TEST_F(ReverseConnectionAddressTest, EmptyConfigValues) {
  ReverseConnectionAddress::ReverseConnectionConfig config;
  config.src_node_id = "";
  config.src_cluster_id = "";
  config.src_tenant_id = "";
  config.remote_cluster = "";
  config.connection_count = 0;

  ReverseConnectionAddress address(config);

  // Should still work with empty values.
  EXPECT_EQ(address.asString(), "127.0.0.1:0");
  EXPECT_EQ(address.logicalName(), "rc://::@:0");

  const auto& retrieved_config = address.reverseConnectionConfig();
  EXPECT_EQ(retrieved_config.src_node_id, "");
  EXPECT_EQ(retrieved_config.src_cluster_id, "");
  EXPECT_EQ(retrieved_config.src_tenant_id, "");
  EXPECT_EQ(retrieved_config.remote_cluster, "");
  EXPECT_EQ(retrieved_config.connection_count, 0);
}

// Test multiple instances with different configurations.
TEST_F(ReverseConnectionAddressTest, MultipleInstances) {
  ReverseConnectionAddress::ReverseConnectionConfig config1;
  config1.src_node_id = "node1";
  config1.src_cluster_id = "cluster1";
  config1.src_tenant_id = "tenant1";
  config1.remote_cluster = "remote1";
  config1.connection_count = 1;

  ReverseConnectionAddress::ReverseConnectionConfig config2;
  config2.src_node_id = "node2";
  config2.src_cluster_id = "cluster2";
  config2.src_tenant_id = "tenant2";
  config2.remote_cluster = "remote2";
  config2.connection_count = 2;

  ReverseConnectionAddress address1(config1);
  ReverseConnectionAddress address2(config2);

  // Should not be equal.
  EXPECT_FALSE(address1 == address2);
  EXPECT_FALSE(address2 == address1);

  // Should have different logical names.
  EXPECT_NE(address1.logicalName(), address2.logicalName());

  // Should have same address string (both use 127.0.0.1:0)
  EXPECT_EQ(address1.asString(), address2.asString());
}

// Test copy constructor and assignment (if implemented).
TEST_F(ReverseConnectionAddressTest, CopyAndAssignment) {
  auto config = createTestConfig();
  ReverseConnectionAddress original(config);

  // Test copy constructor.
  ReverseConnectionAddress copied(original);
  EXPECT_TRUE(original == copied);
  EXPECT_EQ(original.logicalName(), copied.logicalName());
  EXPECT_EQ(original.asString(), copied.asString());

  // Test assignment operator.
  ReverseConnectionAddress::ReverseConnectionConfig config2;
  config2.src_node_id = "different-node";
  config2.src_cluster_id = "different-cluster";
  config2.src_tenant_id = "different-tenant";
  config2.remote_cluster = "different-remote";
  config2.connection_count = 10;

  ReverseConnectionAddress assigned(config2);
  assigned = original;
  EXPECT_TRUE(original == assigned);
  EXPECT_EQ(original.logicalName(), assigned.logicalName());
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
