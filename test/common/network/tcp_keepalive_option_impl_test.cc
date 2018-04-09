#include "common/network/address_impl.h"
#include "common/network/tcp_keepalive_option_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Network {
namespace {

class TcpKeepaliveOptionImplTest : public testing::Test {
public:
  TcpKeepaliveOptionImplTest() { socket_.local_address_.reset(); }

  NiceMock<MockListenSocket> socket_;
  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
};

// We fail to set the option when the underlying setsockopt syscall fails.
TEST_F(TcpKeepaliveOptionImplTest, SetOptionTcpKeepaliveFailure) {
  TcpKeepaliveOptionImpl socket_option{{}, {}, {}};
#ifdef SO_KEEPALIVE
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, SOL_SOCKET, SO_KEEPALIVE, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return -1;
      }));
#endif // SO_KEEPALIVE
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
}

TEST_F(TcpKeepaliveOptionImplTest, SetOptionTcpKeepaliveSuccess) {
  TcpKeepaliveOptionImpl socket_option{{}, {}, {}};
#ifdef SO_KEEPALIVE
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, SOL_SOCKET, SO_KEEPALIVE, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
#else
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
#endif // SO_KEEPALIVE
}

TEST_F(TcpKeepaliveOptionImplTest, SetOptionTcpKeepaliveProbesSuccess) {
  TcpKeepaliveOptionImpl socket_option{3, {}, {}};
#if defined(SO_KEEPALIVE) && defined(TCP_KEEPCNT)
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, SOL_SOCKET, SO_KEEPALIVE, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, IPPROTO_TCP, TCP_KEEPCNT, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(3, *static_cast<const int*>(optval));
        return 0;
      }));
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
#else
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
#endif // defined(SO_KEEPALIVE) && defined(TCP_KEEPCNT)
}

TEST_F(TcpKeepaliveOptionImplTest, SetOptionTcpKeepaliveTimeSuccess) {
  TcpKeepaliveOptionImpl socket_option{{}, 3, {}};
#if defined(SO_KEEPALIVE) && (defined(TCP_KEEPIDLE) || defined(TCP_KEEPALIVE))
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, SOL_SOCKET, SO_KEEPALIVE, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));
#ifdef TCP_KEEPIDLE
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, IPPROTO_TCP, TCP_KEEPIDLE, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(3, *static_cast<const int*>(optval));
        return 0;
      }));
#else
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, IPPROTO_TCP, TCP_KEEPALIVE, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(3, *static_cast<const int*>(optval));
        return 0;
      }));
#endif // TCP_KEEPIDLE
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
#else
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
#endif // defined(SO_KEEPALIVE) && (defined(TCP_KEEPIDLE) || defined(TCP_KEEPALIVE))
}

TEST_F(TcpKeepaliveOptionImplTest, SetOptionTcpKeepaliveIntervalSuccess) {
  TcpKeepaliveOptionImpl socket_option{{}, {}, 3};
#if defined(SO_KEEPALIVE) && defined(TCP_KEEPINTVL)
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, SOL_SOCKET, SO_KEEPALIVE, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, IPPROTO_TCP, TCP_KEEPINTVL, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(3, *static_cast<const int*>(optval));
        return 0;
      }));
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
#else
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
#endif // defined(SO_KEEPALIVE) && defined(TCP_KEEPINTVL)
}

} // namespace
} // namespace Network
} // namespace Envoy
