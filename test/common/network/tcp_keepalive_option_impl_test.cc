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
  TcpKeepaliveOptionImpl socket_option{{}};
  if (ENVOY_SOCKET_SO_KEEPALIVE.has_value()) {
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, ENVOY_SOCKET_SO_KEEPALIVE.value().first, ENVOY_SOCKET_SO_KEEPALIVE.value().second, _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return -1;
        }));
  }
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
}

TEST_F(TcpKeepaliveOptionImplTest, SetOptionTcpKeepaliveSuccess) {
  TcpKeepaliveOptionImpl socket_option{{}};
  if (ENVOY_SOCKET_SO_KEEPALIVE.has_value()) {
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, ENVOY_SOCKET_SO_KEEPALIVE.value().first, ENVOY_SOCKET_SO_KEEPALIVE.value().second, _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  } else {
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  }
}

TEST_F(TcpKeepaliveOptionImplTest, SetOptionTcpKeepaliveProbesSuccess) {
  TcpKeepaliveOptionImpl socket_option{
      Network::TcpKeepaliveConfig{3, absl::nullopt, absl::nullopt}};
  if (ENVOY_SOCKET_SO_KEEPALIVE.has_value() && ENVOY_SOCKET_TCP_KEEPCNT.has_value()) {
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, ENVOY_SOCKET_SO_KEEPALIVE.value().first, ENVOY_SOCKET_SO_KEEPALIVE.value().second, _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, IPPROTO_TCP, ENVOY_SOCKET_TCP_KEEPCNT.value().second, _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(3, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  } else {
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  }
}

TEST_F(TcpKeepaliveOptionImplTest, SetOptionTcpKeepaliveTimeSuccess) {
  TcpKeepaliveOptionImpl socket_option{
      Network::TcpKeepaliveConfig{absl::nullopt, 3, absl::nullopt}};
  if (ENVOY_SOCKET_SO_KEEPALIVE.has_value() && ENVOY_SOCKET_TCP_KEEPIDLE.has_value()) {
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, ENVOY_SOCKET_SO_KEEPALIVE.value().first, ENVOY_SOCKET_SO_KEEPALIVE.value().second, _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, ENVOY_SOCKET_TCP_KEEPIDLE.value().first, ENVOY_SOCKET_TCP_KEEPIDLE.value().second, _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(3, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  } else {
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  }
}

TEST_F(TcpKeepaliveOptionImplTest, SetOptionTcpKeepaliveIntervalSuccess) {
  TcpKeepaliveOptionImpl socket_option{{absl::nullopt, absl::nullopt, 3}};
  if (ENVOY_SOCKET_SO_KEEPALIVE.has_value() && ENVOY_SOCKET_TCP_KEEPINTVL.has_value()) {
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(-1));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, ENVOY_SOCKET_SO_KEEPALIVE.value().first, ENVOY_SOCKET_SO_KEEPALIVE.value().second, _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, ENVOY_SOCKET_TCP_KEEPINTVL.value().first, ENVOY_SOCKET_TCP_KEEPINTVL.value().second, _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(3, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  } else {
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  }
}

} // namespace
} // namespace Network
} // namespace Envoy
