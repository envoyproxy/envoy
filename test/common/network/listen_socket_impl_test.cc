#include "envoy/common/exception.h"

#include "common/network/utility.h"
#include "common/network/listen_socket_impl.h"
#include "test/test_common/network_utility.h"

namespace Network {

TEST(ListenSocket, All) {
  // Test the case of a socket with given address and port, and with bind_to_port set to true.
  auto addr1 = Network::Test::findOrCheckFreePort("127.0.0.2:0", Address::SocketType::Stream);
  TcpListenSocket socket1(addr1, true);
  EXPECT_EQ(0, listen(socket1.fd(), 0));
  EXPECT_EQ(addr1->ip()->port(), socket1.localAddress()->ip()->port());

  // The address and port are bound already, should throw exception.
  EXPECT_THROW(Network::TcpListenSocket socket3(addr1, true), EnvoyException);

  // Test the case of a socket with fd and given address and port.
  TcpListenSocket socket5(dup(socket1.fd()), addr1);
  EXPECT_EQ(addr1->asString(), socket5.localAddress()->asString());
}

} // Network
