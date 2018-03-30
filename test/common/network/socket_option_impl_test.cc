#include "common/network/address_impl.h"
#include "common/network/socket_option_impl.h"

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

class SocketOptionImplTest : public testing::Test {
public:
  SocketOptionImplTest() { socket_.local_address_.reset(); }

  NiceMock<MockListenSocket> socket_;
  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};
};

// We fail to set the option if the socket FD is bad.
TEST_F(SocketOptionImplTest, BadFd) {
  EXPECT_CALL(socket_, fd()).WillOnce(Return(-1));
  EXPECT_EQ(ENOTSUP, SocketOptionImpl::setIpSocketOption(socket_, {}, {}, nullptr, 0));
}

// Nop when there are no socket options set.
TEST_F(SocketOptionImplTest, SetOptionEmptyNop) {
  SocketOptionImpl socket_option{{}, {}};
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PostBind));
}

// We fail to set the option when the underlying setsockopt syscall fails.
TEST_F(SocketOptionImplTest, SetOptionTransparentFailure) {
  SocketOptionImpl socket_option{true, {}};
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
}

// We fail to set the option when the underlying setsockopt syscall fails.
TEST_F(SocketOptionImplTest, SetOptionFreebindFailure) {
  SocketOptionImpl socket_option{{}, true};
  EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
}

// The happy path for setOption(); IP_TRANSPARENT is set to true.
TEST_F(SocketOptionImplTest, SetOptionTransparentSuccessTrue) {
  SocketOptionImpl socket_option{true, {}};
  if (ENVOY_SOCKET_IP_TRANSPARENT.has_value()) {
    Address::Ipv4Instance address("1.2.3.4", 5678);
    const int fd = address.socket(Address::SocketType::Stream);
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, IPPROTO_IP, ENVOY_SOCKET_IP_TRANSPARENT.value(), _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  } else {
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  }
}

// The happy path for setOption(); IP_FREEBIND is set to true.
TEST_F(SocketOptionImplTest, SetOptionFreebindSuccessTrue) {
  SocketOptionImpl socket_option{{}, true};
  if (ENVOY_SOCKET_IP_FREEBIND.has_value()) {
    Address::Ipv4Instance address("1.2.3.4", 5678);
    const int fd = address.socket(Address::SocketType::Stream);
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, IPPROTO_IP, ENVOY_SOCKET_IP_FREEBIND.value(), _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(1, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  } else {
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  }
}

// The happy path for setOpion(); IP_TRANSPARENT is set to false.
TEST_F(SocketOptionImplTest, SetOptionTransparentSuccessFalse) {
  SocketOptionImpl socket_option{false, {}};
  if (ENVOY_SOCKET_IP_TRANSPARENT.has_value()) {
    Address::Ipv4Instance address("1.2.3.4", 5678);
    const int fd = address.socket(Address::SocketType::Stream);
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, IPPROTO_IP, ENVOY_SOCKET_IP_TRANSPARENT.value(), _, sizeof(int)))
        .Times(2)
        .WillRepeatedly(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(0, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
    EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PostBind));
  } else {
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PostBind));
  }
}

// The happy path for setOpion(); IP_FREEBIND is set to false.
TEST_F(SocketOptionImplTest, SetOptionFreebindSuccessFalse) {
  SocketOptionImpl socket_option{{}, false};
  if (ENVOY_SOCKET_IP_FREEBIND.has_value()) {
    Address::Ipv4Instance address("1.2.3.4", 5678);
    const int fd = address.socket(Address::SocketType::Stream);
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
    EXPECT_CALL(os_sys_calls_,
                setsockopt_(_, IPPROTO_IP, ENVOY_SOCKET_IP_FREEBIND.value(), _, sizeof(int)))
        .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
          EXPECT_EQ(0, *static_cast<const int*>(optval));
          return 0;
        }));
    EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  } else {
    EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
  }
}

// Freebind settings have no effect on post-bind behavior.
TEST_F(SocketOptionImplTest, SetOptionFreebindPostBind) {
  SocketOptionImpl socket_option{{}, true};
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PostBind));
}

// If a platform doesn't suppport IPv4 socket option variant for an IPv4 address, we fail
// SocketOptionImpl::setIpSocketOption().
TEST_F(SocketOptionImplTest, V4EmptyOptionNames) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  EXPECT_EQ(ENOTSUP, SocketOptionImpl::setIpSocketOption(socket_, {}, {}, nullptr, 0));
}

// If a platform doesn't suppport IPv4 and IPv6 socket option variants for an IPv4 address, we fail
// SocketOptionImpl::setIpSocketOption().
TEST_F(SocketOptionImplTest, V6EmptyOptionNames) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  EXPECT_EQ(ENOTSUP, SocketOptionImpl::setIpSocketOption(socket_, {}, {}, nullptr, 0));
}

// If a platform suppports IPv4 socket option variant for an IPv4 address,
// SocketOptionImpl::setIpSocketOption() works.
TEST_F(SocketOptionImplTest, V4Only) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IP, 123, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {123}, {}, &option, sizeof(option)));
}

// If a platform suppports IPv4 and IPv6 socket option variants for an IPv4 address,
// SocketOptionImpl::setIpSocketOption() works with the IPv4 variant.
TEST_F(SocketOptionImplTest, V4IgnoreV6) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IP, 123, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {123}, {456}, &option, sizeof(option)));
}

// If a platform suppports IPv6 socket option variant for an IPv6 address,
// SocketOptionImpl::setIpSocketOption() works.
TEST_F(SocketOptionImplTest, V6Only) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IPV6, 456, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {}, {456}, &option, sizeof(option)));
}

// If a platform suppports only the IPv4 variant for an IPv6 address,
// SocketOptionImpl::setIpSocketOption() works with the IPv4 variant.
TEST_F(SocketOptionImplTest, V6OnlyV4Fallback) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IP, 123, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {123}, {}, &option, sizeof(option)));
}

// If a platform suppports IPv4 and IPv6 socket option variants for an IPv6 address,
// SocketOptionImpl::setIpSocketOption() works with the IPv6 variant.
TEST_F(SocketOptionImplTest, V6Precedence) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  const int option = 42;
  EXPECT_CALL(os_sys_calls_, setsockopt_(fd, IPPROTO_IPV6, 456, &option, sizeof(int)));
  EXPECT_EQ(0, SocketOptionImpl::setIpSocketOption(socket_, {123}, {456}, &option, sizeof(option)));
}

} // namespace
} // namespace Network
} // namespace Envoy
