#include "server/listener_socket_option_impl.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Server {

class ListenerSocketOptionImplTest : public testing::Test {
public:
  ListenerSocketOptionImplTest() { socket_.local_address_.reset(); }

  NiceMock<Network::MockListenSocket> socket_;
  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};

  void testSetSocketOptionSuccess(ListenerSocketOptionImpl& socket_option, int socket_level,
                                  Network::SocketOptionName option_name, int option_val,
                                  const std::set<Network::Socket::SocketState>& when) {
    Network::Address::Ipv4Instance address("1.2.3.4", 5678);
    const int fd = address.socket(Network::Address::SocketType::Stream);
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

    for (Network::Socket::SocketState state : when) {
      if (option_name.has_value()) {
        EXPECT_CALL(os_sys_calls_,
                    setsockopt_(_, socket_level, option_name.value(), _, sizeof(int)))
            .WillOnce(Invoke([option_val](int, int, int, const void* optval, socklen_t) -> int {
              EXPECT_EQ(option_val, *static_cast<const int*>(optval));
              return 0;
            }));
        EXPECT_TRUE(socket_option.setOption(socket_, state));
      } else {
        EXPECT_FALSE(socket_option.setOption(socket_, state));
      }
    }

    // The set of SocketState for which this option should not be set. Initialize to all
    // the states, and remove states that are passed in.
    std::list<Network::Socket::SocketState> unset_socketstates{
        Network::Socket::SocketState::PreBind,
        Network::Socket::SocketState::PostBind,
        Network::Socket::SocketState::Listening,
    };
    unset_socketstates.remove_if(
        [&](Network::Socket::SocketState state) -> bool { return when.find(state) != when.end(); });
    for (Network::Socket::SocketState state : unset_socketstates) {
      EXPECT_CALL(os_sys_calls_, setsockopt_(_, _, _, _, _)).Times(0);
      EXPECT_TRUE(socket_option.setOption(socket_, state));
    }
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
