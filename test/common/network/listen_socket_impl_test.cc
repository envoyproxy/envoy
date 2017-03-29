#include "envoy/common/exception.h"

#include "common/network/utility.h"
#include "common/network/listen_socket_impl.h"
#include "test/test_common/network_utility.h"

namespace Network {

TEST(ListenSocket, BindIPv4Port) {
  // Test the case of a socket with given address and port, and with bind_to_port set to true.
  auto addr = Network::Test::findOrCheckFreePort("127.0.0.2:0", Address::SocketType::Stream);
  EXPECT_LT(0U, addr->ip()->port());
  TcpListenSocket socket1(addr, true);
  EXPECT_EQ(0, listen(socket1.fd(), 0));
  EXPECT_EQ(addr->ip()->port(), socket1.localAddress()->ip()->port());
  EXPECT_EQ("127.0.0.2", socket1.localAddress()->ip()->addressAsString());

  // The address and port are bound already, should throw exception.
  EXPECT_THROW(Network::TcpListenSocket socket2(addr, true), EnvoyException);

  // Test the case of a socket with fd and given address and port.
  TcpListenSocket socket3(dup(socket1.fd()), addr);
  EXPECT_EQ(addr->asString(), socket3.localAddress()->asString());
}

TEST(ListenSocket, BindIPv6Port) {
  // Test the case of a socket with given address and port, and with bind_to_port set to true.
  auto addr = Network::Test::findOrCheckFreePort("[::1]:0", Address::SocketType::Stream);
  EXPECT_LT(0U, addr->ip()->port());
  TcpListenSocket socket1(addr, true);
  EXPECT_EQ(0, listen(socket1.fd(), 0));
  EXPECT_EQ(addr->ip()->port(), socket1.localAddress()->ip()->port());
  EXPECT_EQ("::1", socket1.localAddress()->ip()->addressAsString());

  // The address and port are bound already, should throw exception.
  EXPECT_THROW(Network::TcpListenSocket socket2(addr, true), EnvoyException);

  // Test the case of a socket with fd and given address and port.
  TcpListenSocket socket3(dup(socket1.fd()), addr);
  EXPECT_EQ(addr->asString(), socket3.localAddress()->asString());
}

// Validate we get port allocation when binding to port zero.
TEST(ListenSocket, BindIPv4PortZero) {
  TcpListenSocket socket(Utility::getCanonicalIpv4LoopbackAddress(), true);
  EXPECT_EQ(Address::Type::Ip, socket.localAddress()->type());
  EXPECT_EQ("127.0.0.1", socket.localAddress()->ip()->addressAsString());
  EXPECT_GT(socket.localAddress()->ip()->port(), 0U);
  EXPECT_EQ(Address::IpVersion::v4, socket.localAddress()->ip()->version());
}

// Validate we get port allocation when binding to port zero.
TEST(ListenSocket, BindIPv6PortZero) {
  TcpListenSocket socket(Utility::getIpv6LoopbackAddress(), true);
  EXPECT_EQ(Address::Type::Ip, socket.localAddress()->type());
  EXPECT_EQ("::1", socket.localAddress()->ip()->addressAsString());
  EXPECT_GT(socket.localAddress()->ip()->port(), 0U);
  EXPECT_EQ(Address::IpVersion::v6, socket.localAddress()->ip()->version());
}

} // Network
