#include "source/common/network/address_impl.h"
#include "test/extensions/clusters/reverse_connection/reverse_connection_cluster_test_base.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

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

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
