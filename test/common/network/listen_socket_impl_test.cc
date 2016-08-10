#include "envoy/common/exception.h"

#include "common/network/listen_socket_impl.h"

namespace Network {

TEST(ListenSocket, All) {
  TcpListenSocket socket1(15000);
  listen(socket1.fd(), 0);
  EXPECT_EQ(15000U, socket1.port());

  EXPECT_THROW(Network::TcpListenSocket socket2(15000), EnvoyException);

  TcpListenSocket socket2(dup(socket1.fd()), 15000);
  EXPECT_EQ(15000U, socket2.port());
}

} // Network
