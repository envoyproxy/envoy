#include "common/network/addr_family_aware_socket_option_impl.h"

#include "test/common/network/socket_option_test.h"

namespace Envoy {
namespace Network {
namespace {

class AddrFailyAwareSocketOptionImplTest : public SocketOptionTest {};

// We fail to set the option when the underlying setsockopt syscall fails.
TEST_F(AddrFailyAwareSocketOptionImplTest, SetOptionFailure) {
  EXPECT_CALL(socket_, fd()).WillOnce(Return(-1));
  AddrFailyAwareSocketOptionImpl socket_option{
      Socket::SocketState::PreBind, Network::SocketOptionName(std::make_pair(5, 10)), {}, 1};
  EXPECT_LOG_CONTAINS(
      "warning", "Failed to set IP socket option on non-IP socket",
      EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind)););
}

// If a platform supports IPv4 socket option variant for an IPv4 address, it works
TEST_F(AddrFailyAwareSocketOptionImplTest, SetOptionSuccess) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFailyAwareSocketOptionImpl socket_option{
      Socket::SocketState::PreBind, Network::SocketOptionName(std::make_pair(5, 10)), {}, 1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(5, 10)), 1,
                             {Socket::SocketState::PreBind});
}

// If a platform doesn't support IPv4 socket option variant for an IPv4 address we fail
TEST_F(AddrFailyAwareSocketOptionImplTest, V4EmptyOptionNames) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  AddrFailyAwareSocketOptionImpl socket_option{Socket::SocketState::PreBind, {}, {}, 1};

  EXPECT_LOG_CONTAINS("warning", "Setting option on socket failed: Operation not supported",
                      EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind)));
}

// If a platform doesn't support IPv4 and IPv6 socket option variants for an IPv4 address, we fail
TEST_F(AddrFailyAwareSocketOptionImplTest, V6EmptyOptionNames) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  AddrFailyAwareSocketOptionImpl socket_option{Socket::SocketState::PreBind, {}, {}, 1};

  EXPECT_LOG_CONTAINS("warning", "Setting option on socket failed: Operation not supported",
                      EXPECT_FALSE(socket_option.setOption(socket_, Socket::SocketState::PreBind)));
}

// If a platform suppports IPv4 and IPv6 socket option variants for an IPv4 address, we apply the
// IPv4 varient
TEST_F(AddrFailyAwareSocketOptionImplTest, V4IgnoreV6) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFailyAwareSocketOptionImpl socket_option{Socket::SocketState::PreBind,
                                               Network::SocketOptionName(std::make_pair(5, 10)),
                                               Network::SocketOptionName(std::make_pair(6, 11)), 1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(5, 10)), 1,
                             {Socket::SocketState::PreBind});
}

// If a platform suppports IPv6 socket option variant for an IPv6 address it works
TEST_F(AddrFailyAwareSocketOptionImplTest, V6Only) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFailyAwareSocketOptionImpl socket_option{
      Socket::SocketState::PreBind, {}, Network::SocketOptionName(std::make_pair(6, 11)), 1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(6, 11)), 1,
                             {Socket::SocketState::PreBind});
}

// If a platform suppports only the IPv4 variant for an IPv6 address,
// we apply the IPv4 variant.
TEST_F(AddrFailyAwareSocketOptionImplTest, V6OnlyV4Fallback) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFailyAwareSocketOptionImpl socket_option{
      Socket::SocketState::PreBind, Network::SocketOptionName(std::make_pair(5, 10)), {}, 1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(5, 10)), 1,
                             {Socket::SocketState::PreBind});
}

// If a platform suppports IPv4 and IPv6 socket option variants for an IPv6 address,
// AddrFailyAwareSocketOptionImpl::setIpSocketOption() works with the IPv6 variant.
TEST_F(AddrFailyAwareSocketOptionImplTest, V6Precedence) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFailyAwareSocketOptionImpl socket_option{Socket::SocketState::PreBind,
                                               Network::SocketOptionName(std::make_pair(5, 10)),
                                               Network::SocketOptionName(std::make_pair(6, 11)), 1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(6, 11)), 1,
                             {Socket::SocketState::PreBind});
}

} // namespace
} // namespace Network
} // namespace Envoy
