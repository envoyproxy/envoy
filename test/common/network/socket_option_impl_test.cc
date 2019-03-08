#include "test/common/network/socket_option_test.h"

namespace Envoy {
namespace Network {
namespace {

class SocketOptionImplTest : public SocketOptionTest {};

TEST_F(SocketOptionImplTest, BadFd) {
  absl::string_view zero("\0\0\0\0", 4);
  Api::SysCallIntResult result = SocketOptionImpl::setSocketOption(socket_, {}, zero);
  EXPECT_EQ(-1, result.rc_);
  EXPECT_EQ(ENOTSUP, result.errno_);
}

TEST_F(SocketOptionImplTest, SetOptionSuccessTrue) {
  SocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_PREBIND,
                                 Network::SocketOptionName(std::make_pair(5, 10)), 1};
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, 5, 10, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));
  EXPECT_TRUE(socket_option.setOption(socket_, envoy::api::v2::core::SocketOption::STATE_PREBIND));
}

TEST_F(SocketOptionImplTest, GetOptionDetailsCorrectState) {
  SocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_PREBIND,
                                 Network::SocketOptionName(std::make_pair(5, 10)), 1};

  auto result =
      socket_option.getOptionDetails(socket_, envoy::api::v2::core::SocketOption::STATE_PREBIND);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, makeDetails(std::make_pair(5, 10), 1));
}

TEST_F(SocketOptionImplTest, GetMoreOptionDetailsCorrectState) {
  SocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_LISTENING,
                                 Network::SocketOptionName(std::make_pair(7, 9)), 5};

  auto result =
      socket_option.getOptionDetails(socket_, envoy::api::v2::core::SocketOption::STATE_LISTENING);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, makeDetails(std::make_pair(7, 9), 5));
}

TEST_F(SocketOptionImplTest, GetOptionDetailsFailureWrongState) {
  SocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_LISTENING,
                                 Network::SocketOptionName(std::make_pair(7, 9)), 5};

  auto result =
      socket_option.getOptionDetails(socket_, envoy::api::v2::core::SocketOption::STATE_BOUND);
  EXPECT_FALSE(result.has_value());
}

TEST_F(SocketOptionImplTest, GetUnsupportedOptReturnsNullopt) {
  SocketOptionImpl socket_option{envoy::api::v2::core::SocketOption::STATE_LISTENING,
                                 Network::SocketOptionName(absl::nullopt), 5};

  auto result =
      socket_option.getOptionDetails(socket_, envoy::api::v2::core::SocketOption::STATE_LISTENING);
  EXPECT_FALSE(result.has_value());
}

} // namespace
} // namespace Network
} // namespace Envoy
