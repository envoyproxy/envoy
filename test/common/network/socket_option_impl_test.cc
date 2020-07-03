#include "envoy/config/core/v3/base.pb.h"

#include "test/common/network/socket_option_test.h"

namespace Envoy {
namespace Network {
namespace {

class SocketOptionImplTest : public SocketOptionTest {};

TEST_F(SocketOptionImplTest, BadFd) {
  absl::string_view zero("\0\0\0\0", 4);
  Api::SysCallIntResult result =
      SocketOptionImpl::setSocketOption(socket_, {}, zero.data(), zero.size());
  EXPECT_EQ(-1, result.rc_);
  EXPECT_EQ(SOCKET_ERROR_NOT_SUP, result.errno_);
}

TEST_F(SocketOptionImplTest, HasName) {
  auto optname = ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_SNDBUF);

  // Verify that the constructor macro sets all the fields correctly.
  EXPECT_TRUE(optname.hasValue());
  EXPECT_EQ(SOL_SOCKET, optname.level());
  EXPECT_EQ(SO_SNDBUF, optname.option());
  EXPECT_EQ("SOL_SOCKET/SO_SNDBUF", optname.name());

  // The default constructor should not have a value, i.e. should
  // be unsupported.
  EXPECT_FALSE(SocketOptionName().hasValue());

  // If we fail to set an option, verify that the log message
  // contains the option name so the operator can debug.
  SocketOptionImpl socket_option{envoy::config::core::v3::SocketOption::STATE_PREBIND, optname, 1};
  EXPECT_CALL(socket_, setSocketOption(_, _, _, _))
      .WillOnce(Invoke([](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return {-1, 0};
      }));

  EXPECT_LOG_CONTAINS(
      "warning", "Setting SOL_SOCKET/SO_SNDBUF option on socket failed",
      socket_option.setOption(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND));
}

TEST_F(SocketOptionImplTest, SetOptionSuccessTrue) {
  SocketOptionImpl socket_option{envoy::config::core::v3::SocketOption::STATE_PREBIND,
                                 ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10), 1};
  EXPECT_CALL(socket_, setSocketOption(5, 10, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, const void* optval, socklen_t) -> Api::SysCallIntResult {
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return {0, 0};
      }));
  EXPECT_TRUE(
      socket_option.setOption(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND));
}

TEST_F(SocketOptionImplTest, GetOptionDetailsCorrectState) {
  SocketOptionImpl socket_option{envoy::config::core::v3::SocketOption::STATE_PREBIND,
                                 ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10), 1};

  auto result =
      socket_option.getOptionDetails(socket_, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, makeDetails(ENVOY_MAKE_SOCKET_OPTION_NAME(5, 10), 1));
}

TEST_F(SocketOptionImplTest, GetMoreOptionDetailsCorrectState) {
  SocketOptionImpl socket_option{envoy::config::core::v3::SocketOption::STATE_LISTENING,
                                 ENVOY_MAKE_SOCKET_OPTION_NAME(7, 9), 5};

  auto result = socket_option.getOptionDetails(
      socket_, envoy::config::core::v3::SocketOption::STATE_LISTENING);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, makeDetails(ENVOY_MAKE_SOCKET_OPTION_NAME(7, 9), 5));
}

TEST_F(SocketOptionImplTest, GetOptionDetailsFailureWrongState) {
  SocketOptionImpl socket_option{envoy::config::core::v3::SocketOption::STATE_LISTENING,
                                 ENVOY_MAKE_SOCKET_OPTION_NAME(7, 9), 5};

  auto result =
      socket_option.getOptionDetails(socket_, envoy::config::core::v3::SocketOption::STATE_BOUND);
  EXPECT_FALSE(result.has_value());
}

TEST_F(SocketOptionImplTest, GetUnsupportedOptReturnsNullopt) {
  SocketOptionImpl socket_option{envoy::config::core::v3::SocketOption::STATE_LISTENING,
                                 Network::SocketOptionName(), 5};

  auto result = socket_option.getOptionDetails(
      socket_, envoy::config::core::v3::SocketOption::STATE_LISTENING);
  EXPECT_FALSE(result.has_value());
}

} // namespace
} // namespace Network
} // namespace Envoy
