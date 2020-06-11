#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/network/addr_family_aware_socket_option_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/socket_interface_impl.h"
#include "common/network/utility.h"

#include "test/common/network/socket_option_test.h"

namespace Envoy {
namespace Network {
namespace {

class AddrFamilyAwareSocketOptionImplTest : public SocketOptionTest {
protected:
  void SetUp() override {
    EXPECT_CALL(os_sys_calls_, socket)
        .WillRepeatedly(Invoke([this](int domain, int type, int protocol) {
          return os_sys_calls_actual_.socket(domain, type, protocol);
        }));
    EXPECT_CALL(os_sys_calls_, close(_)).Times(testing::AnyNumber());
  }
};

// We fail to set the option when the underlying setsockopt syscall fails.
TEST_F(AddrFamilyAwareSocketOptionImplTest, SetOptionFailure) {
  EXPECT_CALL(socket_, ipVersion).WillRepeatedly(testing::Return(absl::nullopt));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10),
      {},
      1};
  EXPECT_LOG_CONTAINS("warning", "Failed to set IP socket option on non-IP socket",
                      EXPECT_FALSE(socket_option.setOption(
                          socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
}

// If a platform supports IPv4 socket option variant for an IPv4 address, it works
TEST_F(AddrFamilyAwareSocketOptionImplTest, SetOptionSuccess) {
  EXPECT_CALL(socket_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v4));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10),
      {},
      1};
  testSetSocketOptionSuccess(socket_option, ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10), 1,
                             {envoy::config::core::v3::SocketOption::STATE_PREBIND});
}

// If a platform doesn't support IPv4 socket option variant for an IPv4 address we fail
TEST_F(AddrFamilyAwareSocketOptionImplTest, V4EmptyOptionNames) {
  EXPECT_CALL(socket_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v4));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND, {}, {}, 1};
  EXPECT_LOG_CONTAINS("warning", "Failed to set unsupported option on socket",
                      EXPECT_FALSE(socket_option.setOption(
                          socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
}

// If a platform doesn't support IPv4 and IPv6 socket option variants for an IPv4 address, we fail
TEST_F(AddrFamilyAwareSocketOptionImplTest, V6EmptyOptionNames) {
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND, {}, {}, 1};
  EXPECT_LOG_CONTAINS("warning", "Failed to set unsupported option on socket",
                      EXPECT_FALSE(socket_option.setOption(
                          socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND)));
}

// If a platform supports IPv4 and IPv6 socket option variants for an IPv4 address, we apply the
// IPv4 variant
TEST_F(AddrFamilyAwareSocketOptionImplTest, V4IgnoreV6) {
  EXPECT_CALL(socket_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v4));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10),
      ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11), 1};
  testSetSocketOptionSuccess(socket_option, ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10), 1,
                             {envoy::config::core::v3::SocketOption::STATE_PREBIND});
}

// If a platform supports IPv6 socket option variant for an IPv6 address it works
TEST_F(AddrFamilyAwareSocketOptionImplTest, V6Only) {
  EXPECT_CALL(socket_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v6));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      {},
      ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11),
      1};
  testSetSocketOptionSuccess(socket_option, ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11), 1,
                             {envoy::config::core::v3::SocketOption::STATE_PREBIND});
}

// If a platform supports only the IPv4 variant for an IPv6 address,
// we apply the IPv4 variant.
TEST_F(AddrFamilyAwareSocketOptionImplTest, V6OnlyV4Fallback) {
  EXPECT_CALL(socket_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v6));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10),
      {},
      1};
  testSetSocketOptionSuccess(socket_option, ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10), 1,
                             {envoy::config::core::v3::SocketOption::STATE_PREBIND});
}

// If a platform supports IPv4 and IPv6 socket option variants for an IPv6 address,
// AddrFamilyAwareSocketOptionImpl::setIpSocketOption() works with the IPv6 variant.
TEST_F(AddrFamilyAwareSocketOptionImplTest, V6Precedence) {
  EXPECT_CALL(socket_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v6));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10),
      ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11), 1};
  testSetSocketOptionSuccess(socket_option, ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11), 1,
                             {envoy::config::core::v3::SocketOption::STATE_PREBIND});
}

// GetSocketOptionName returns the v4 information for a v4 address
TEST_F(AddrFamilyAwareSocketOptionImplTest, V4GetSocketOptionName) {
  EXPECT_CALL(socket_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v4));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10),
      ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11), 1};
  auto result =
      socket_option.getOptionDetails(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), makeDetails(ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10), 1));
}

// GetSocketOptionName returns the v4 information for a v6 address
TEST_F(AddrFamilyAwareSocketOptionImplTest, V6GetSocketOptionName) {
  EXPECT_CALL(socket_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v6));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10),
      ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11), 5};
  auto result =
      socket_option.getOptionDetails(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), makeDetails(ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11), 5));
}

// GetSocketOptionName returns nullopt if the state is wrong
TEST_F(AddrFamilyAwareSocketOptionImplTest, GetSocketOptionWrongState) {
  EXPECT_CALL(socket_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v6));
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10),
      ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11), 5};
  auto result =
      socket_option.getOptionDetails(socket_, envoy::config::core::v3::SocketOption::STATE_BOUND);
  EXPECT_FALSE(result.has_value());
}

// GetSocketOptionName returns nullopt if the version could not be determined
TEST_F(AddrFamilyAwareSocketOptionImplTest, GetSocketOptionCannotDetermineVersion) {
  AddrFamilyAwareSocketOptionImpl socket_option{
      envoy::config::core::v3::SocketOption::STATE_PREBIND, ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10),
      ENVOY_MAKE_SOCKET_OPTION_NAME(6, 11), 5};

  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>();
  EXPECT_CALL(socket_, ipVersion).WillOnce(testing::Return(absl::nullopt));
  auto result =
      socket_option.getOptionDetails(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  EXPECT_FALSE(result.has_value());
}

} // namespace
} // namespace Network
} // namespace Envoy
