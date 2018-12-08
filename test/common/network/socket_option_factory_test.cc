#include "common/network/address_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Network {

class SocketOptionFactoryTest : public testing::Test {
public:
  SocketOptionFactoryTest() = default;

protected:
  testing::NiceMock<MockListenSocket> socket_mock_;
  Api::MockOsSysCalls os_sys_calls_mock_;

  void SetUp() override { socket_mock_.local_address_.reset(); }
  void makeSocketV4() {
    socket_mock_.local_address_ = std::make_unique<Address::Ipv4Instance>("1.2.3.4", 5678);
  }
  void makeSocketV6() {
    socket_mock_.local_address_ = std::make_unique<Address::Ipv6Instance>("::1:2:3:4", 5678);
  }

  absl::optional<Network::Socket::Option::Details>
  findSocketOptionInfo(const Network::Socket::Options& options,
                       const Network::SocketOptionName& name,
                       envoy::api::v2::core::SocketOption::SocketState state) {
    for (const auto& option : options) {
      auto info = option->getOptionDetails(socket_mock_, state);
      if (info.has_value() && info->name_ == name) {
        return info;
      }
    }

    return absl::nullopt;
  }

  std::string intToBinaryString(int value) {
    return std::string{absl::string_view(reinterpret_cast<const char*>(&value), sizeof(value))};
  }
};

#define CHECK_OPTION_SUPPORTED(option)                                                             \
  if (!option.has_value()) {                                                                       \
    return;                                                                                        \
  }

TEST_F(SocketOptionFactoryTest, TestBuildSocketMarkOptions) {
  CHECK_OPTION_SUPPORTED(ENVOY_SOCKET_SO_MARK);
  auto options = SocketOptionFactory::buildSocketMarkOptions(100);

  auto applied_option = findSocketOptionInfo(*options, ENVOY_SOCKET_SO_MARK,
                                             envoy::api::v2::core::SocketOption::STATE_PREBIND);
  ASSERT_TRUE(applied_option.has_value());
  EXPECT_EQ(intToBinaryString(100), applied_option->value_);
}

TEST_F(SocketOptionFactoryTest, TestBuildIpv4TransparentOptions) {
  CHECK_OPTION_SUPPORTED(ENVOY_SOCKET_IP_TRANSPARENT);
  makeSocketV4();

  auto options = SocketOptionFactory::buildIpTransparentOptions();

  auto prebind_option = findSocketOptionInfo(*options, ENVOY_SOCKET_IP_TRANSPARENT,
                                             envoy::api::v2::core::SocketOption::STATE_PREBIND);
  auto bound_option = findSocketOptionInfo(*options, ENVOY_SOCKET_IP_TRANSPARENT,
                                           envoy::api::v2::core::SocketOption::STATE_BOUND);
  ASSERT_TRUE(prebind_option.has_value());
  EXPECT_EQ(intToBinaryString(1), prebind_option->value_);
  ASSERT_TRUE(bound_option.has_value());
  EXPECT_EQ(intToBinaryString(1), bound_option->value_);
}

TEST_F(SocketOptionFactoryTest, TestBuildIpv6TransparentOptions) {
  CHECK_OPTION_SUPPORTED(ENVOY_SOCKET_IPV6_TRANSPARENT);
  makeSocketV6();

  auto options = SocketOptionFactory::buildIpTransparentOptions();

  auto prebind_option = findSocketOptionInfo(*options, ENVOY_SOCKET_IPV6_TRANSPARENT,
                                             envoy::api::v2::core::SocketOption::STATE_PREBIND);
  auto bound_option = findSocketOptionInfo(*options, ENVOY_SOCKET_IPV6_TRANSPARENT,
                                           envoy::api::v2::core::SocketOption::STATE_BOUND);
  ASSERT_TRUE(prebind_option.has_value());
  EXPECT_EQ(intToBinaryString(1), prebind_option->value_);
  ASSERT_TRUE(bound_option.has_value());
  EXPECT_EQ(intToBinaryString(1), bound_option->value_);
}

} // namespace Network
} // namespace Envoy
