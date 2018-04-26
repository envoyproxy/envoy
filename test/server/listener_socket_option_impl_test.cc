#include "server/listener_socket_option_impl.h"

#include "test/common/network/socket_option_test.h"

namespace Envoy {
namespace Server {

class ListenerSocketOptionImplTest : public Network::SocketOptionTest {
public:
  void testSetSocketOptionSuccess(ListenerSocketOptionImpl& socket_option, int socket_level,
                                  Network::SocketOptionName option_name, int option_val,
                                  const std::set<Network::Socket::SocketState>& when) {
    Network::SocketOptionTest::testSetSocketOptionSuccess(socket_option, socket_level, option_name,
                                                          option_val, when);
  }
};

// We fail to set the tcp-fastopen option when the underlying setsockopt syscall fails.
TEST_F(ListenerSocketOptionImplTest, SetOptionTcpFastopenFailure) {
  if (ENVOY_SOCKET_TCP_FASTOPEN.has_value()) {
    ListenerSocketOptionImpl socket_option{{}, {}, 1};
    EXPECT_CALL(os_sys_calls_, setsockopt_(_, IPPROTO_TCP, ENVOY_SOCKET_TCP_FASTOPEN.value(), _, _))
        .WillOnce(Return(-1));
    EXPECT_FALSE(socket_option.setOption(socket_, Network::Socket::SocketState::Listening));
  }
}

// The happy path for setOption(); TCP_FASTOPEN is set to true.
TEST_F(ListenerSocketOptionImplTest, SetOptionTcpFastopenSuccessTrue) {
  ListenerSocketOptionImpl socket_option{{}, {}, 42};
  testSetSocketOptionSuccess(socket_option, IPPROTO_TCP, ENVOY_SOCKET_TCP_FASTOPEN, 42,
                             {Network::Socket::SocketState::Listening});
}

// The happy path for setOption(); TCP_FASTOPEN is set to false.
TEST_F(ListenerSocketOptionImplTest, SetOptionTcpFastopenSuccessFalse) {
  ListenerSocketOptionImpl socket_option{{}, {}, 0};
  testSetSocketOptionSuccess(socket_option, IPPROTO_TCP, ENVOY_SOCKET_TCP_FASTOPEN, 0,
                             {Network::Socket::SocketState::Listening});
}

} // namespace Server
} // namespace Envoy
