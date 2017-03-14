#include "envoy/common/exception.h"

#include "common/network/listen_socket_impl.h"

namespace Network {

TEST(ListenSocket, All) {
  TcpListenSocket socket1(uint32_t(15000), true);
  listen(socket1.fd(), 0);
  EXPECT_EQ(15000U, socket1.localAddress()->ip()->port());

  TcpListenSocket socket2("tcp://127.0.0.1:15002", true);
  listen(socket2.fd(), 0);
  EXPECT_EQ("127.0.0.1:15002", socket2.localAddress()->asString());

  EXPECT_THROW(Network::TcpListenSocket socket3(uint32_t(15000), true), EnvoyException);
  EXPECT_THROW(Network::TcpListenSocket socket4("tcp://127.0.0.1:15002", true), EnvoyException);

  TcpListenSocket socket5(dup(socket1.fd()), 15000);
  EXPECT_EQ(15000U, socket5.localAddress()->ip()->port());

  TcpListenSocket socket6(dup(socket1.fd()), std::string("tcp://127.0.0.1:15004"));
  EXPECT_EQ("127.0.0.1:15004", socket6.localAddress()->asString());
}

} // Network
