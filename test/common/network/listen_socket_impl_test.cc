#include "envoy/common/exception.h"

#include "common/network/listen_socket_impl.h"

namespace Network {

TEST(ListenSocket, All) {
  // Test the case of a socket with given port and bind_to_port set to true.
  TcpListenSocket socket1(uint32_t(15000), true);
  EXPECT_EQ(1, listen(socket1.fd(), 0));
  EXPECT_EQ(15000U, socket1.localAddress()->ip()->port());

  // Test the case of a socket with given tcp address and bind_to_port set to true.
  TcpListenSocket socket2("tcp://127.0.0.1:15002", true);
  EXPECT_EQ(1, listen(socket2.fd(), 0));
  EXPECT_EQ("127.0.0.1:15002", socket2.localAddress()->asString());

  // The port is bound already, should throw exception.
  EXPECT_THROW(Network::TcpListenSocket socket3(uint32_t(15000), true), EnvoyException);

  // The address is bound already, should throw exception.
  EXPECT_THROW(Network::TcpListenSocket socket4("tcp://127.0.0.1:15002", true), EnvoyException);

  // Test the case of a socket with fd and given port.
  TcpListenSocket socket5(dup(socket1.fd()), 15000);
  EXPECT_EQ(15000U, socket5.localAddress()->ip()->port());

  // Test the case of a socket with fd and given tcp address.
  TcpListenSocket socket6(dup(socket1.fd()), std::string("tcp://127.0.0.1:15004"));
  EXPECT_EQ("127.0.0.1:15004", socket6.localAddress()->asString());
}

} // Network
