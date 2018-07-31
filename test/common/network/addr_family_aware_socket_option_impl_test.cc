#include "common/network/addr_family_aware_socket_option_impl.h"

#include "test/common/network/socket_option_test.h"

namespace Envoy {
namespace Network {
namespace {

class AddrFamilyAwareSocketOptionImplTest : public SocketOptionTest {};

// We fail to set the option when the underlying setsockopt syscall fails.
TEST_F(AddrFamilyAwareSocketOptionImplTest, SetOptionFailure) {
  EXPECT_CALL(socket_, fd()).WillOnce(Return(-1));
  AddrFamilyAwareSocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_PREBIND,
                                                Network::SocketOptionName(std::make_pair(5, 10)),
                                                {},
                                                1};
  EXPECT_LOG_CONTAINS("warning", "Failed to set IP socket option on non-IP socket",
                      EXPECT_FALSE(socket_option.setOption(
                          socket_, envoy::api::v2::core::SocketOption::STATE_PREBIND)));
}

// If a platform supports IPv4 socket option variant for an IPv4 address, it works
TEST_F(AddrFamilyAwareSocketOptionImplTest, SetOptionSuccess) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFamilyAwareSocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_PREBIND,
                                                Network::SocketOptionName(std::make_pair(5, 10)),
                                                {},
                                                1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(5, 10)), 1,
                             {envoy::api::v2::core::SocketOption::STATE_PREBIND});
}

// If a platform doesn't support IPv4 socket option variant for an IPv4 address we fail
TEST_F(AddrFamilyAwareSocketOptionImplTest, V4EmptyOptionNames) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::api::v2::core::SocketOption::STATE_PREBIND, {}, {}, 1};

  EXPECT_LOG_CONTAINS("warning", "Setting option on socket failed: Operation not supported",
                      EXPECT_FALSE(socket_option.setOption(
                          socket_, envoy::api::v2::core::SocketOption::STATE_PREBIND)));
}

// If a platform doesn't support IPv4 and IPv6 socket option variants for an IPv4 address, we fail
TEST_F(AddrFamilyAwareSocketOptionImplTest, V6EmptyOptionNames) {
  EXPECT_CALL(os_sys_calls_, socket(_, _, _));
  EXPECT_CALL(os_sys_calls_, close(_));
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::api::v2::core::SocketOption::STATE_PREBIND, {}, {}, 1};

  EXPECT_LOG_CONTAINS("warning", "Setting option on socket failed: Operation not supported",
                      EXPECT_FALSE(socket_option.setOption(
                          socket_, envoy::api::v2::core::SocketOption::STATE_PREBIND)));
}

// If a platform suppports IPv4 and IPv6 socket option variants for an IPv4 address, we apply the
// IPv4 varient
TEST_F(AddrFamilyAwareSocketOptionImplTest, V4IgnoreV6) {
  Address::Ipv4Instance address("1.2.3.4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFamilyAwareSocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_PREBIND,
                                                Network::SocketOptionName(std::make_pair(5, 10)),
                                                Network::SocketOptionName(std::make_pair(6, 11)),
                                                1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(5, 10)), 1,
                             {envoy::api::v2::core::SocketOption::STATE_PREBIND});
}

// If a platform suppports IPv6 socket option variant for an IPv6 address it works
TEST_F(AddrFamilyAwareSocketOptionImplTest, V6Only) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFamilyAwareSocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_PREBIND,
                                                {},
                                                Network::SocketOptionName(std::make_pair(6, 11)),
                                                1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(6, 11)), 1,
                             {envoy::api::v2::core::SocketOption::STATE_PREBIND});
}

// If a platform suppports only the IPv4 variant for an IPv6 address,
// we apply the IPv4 variant.
TEST_F(AddrFamilyAwareSocketOptionImplTest, V6OnlyV4Fallback) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFamilyAwareSocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_PREBIND,
                                                Network::SocketOptionName(std::make_pair(5, 10)),
                                                {},
                                                1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(5, 10)), 1,
                             {envoy::api::v2::core::SocketOption::STATE_PREBIND});
}

// If a platform suppports IPv4 and IPv6 socket option variants for an IPv6 address,
// AddrFamilyAwareSocketOptionImpl::setIpSocketOption() works with the IPv6 variant.
TEST_F(AddrFamilyAwareSocketOptionImplTest, V6Precedence) {
  Address::Ipv6Instance address("::1:2:3:4", 5678);
  const int fd = address.socket(Address::SocketType::Stream);
  EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

  AddrFamilyAwareSocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_PREBIND,
                                                Network::SocketOptionName(std::make_pair(5, 10)),
                                                Network::SocketOptionName(std::make_pair(6, 11)),
                                                1};
  testSetSocketOptionSuccess(socket_option, Network::SocketOptionName(std::make_pair(6, 11)), 1,
                             {envoy::api::v2::core::SocketOption::STATE_PREBIND});
}

} // namespace
} // namespace Network
} // namespace Envoy
