#include "common/network/address_impl.h"
#include "common/network/socket_option_factory.h"
#include "common/network/socket_option_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Network {
namespace {

class SocketOptionFactoryTest : public testing::Test {
public:
  SocketOptionFactoryTest() = default;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{[this]() {
    // Before injecting OsSysCallsImpl, make sure validateIpv{4,6}Supported is called so the static
    // bool is initialized without requiring to mock ::socket and ::close. :( :(
    std::make_unique<Address::Ipv4Instance>("1.2.3.4", 5678);
    std::make_unique<Address::Ipv6Instance>("::1:2:3:4", 5678);
    return &os_sys_calls_mock_;
  }()};

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
};

#define CHECK_OPTION_SUPPORTED(option)                                                             \
  if (!option.has_value()) {                                                                       \
    return;                                                                                        \
  }

// TODO(klarose): Simplify these tests once https://github.com/envoyproxy/envoy/pull/5351 is merged.

TEST_F(SocketOptionFactoryTest, TestBuildSocketMarkOptions) {

  // use a shared_ptr due to applyOptions requiring one
  std::shared_ptr<Socket::Options> options = SocketOptionFactory::buildSocketMarkOptions(100);

  const auto expected_option = ENVOY_SOCKET_SO_MARK;
  CHECK_OPTION_SUPPORTED(expected_option);

  const int type = expected_option.value().first;
  const int option = expected_option.value().second;
  EXPECT_CALL(os_sys_calls_mock_, setsockopt_(_, _, _, _, sizeof(int)))
      .WillOnce(Invoke([type, option](int, int input_type, int input_option, const void* optval,
                                      socklen_t) -> int {
        EXPECT_EQ(100, *static_cast<const int*>(optval));
        EXPECT_EQ(type, input_type);
        EXPECT_EQ(option, input_option);
        return 0;
      }));

  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_PREBIND));
}

TEST_F(SocketOptionFactoryTest, TestBuildIpv4TransparentOptions) {
  makeSocketV4();

  // use a shared_ptr due to applyOptions requiring one
  std::shared_ptr<Socket::Options> options = SocketOptionFactory::buildIpTransparentOptions();

  const auto expected_option = ENVOY_SOCKET_IP_TRANSPARENT;
  CHECK_OPTION_SUPPORTED(expected_option);

  const int type = expected_option.value().first;
  const int option = expected_option.value().second;
  EXPECT_CALL(os_sys_calls_mock_, setsockopt_(_, _, _, _, sizeof(int)))
      .Times(2)
      .WillRepeatedly(Invoke([type, option](int, int input_type, int input_option,
                                            const void* optval, socklen_t) -> int {
        EXPECT_EQ(type, input_type);
        EXPECT_EQ(option, input_option);
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));

  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_PREBIND));
  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_BOUND));
}

TEST_F(SocketOptionFactoryTest, TestBuildIpv6TransparentOptions) {
  makeSocketV6();

  // use a shared_ptr due to applyOptions requiring one
  std::shared_ptr<Socket::Options> options = SocketOptionFactory::buildIpTransparentOptions();

  const auto expected_option = ENVOY_SOCKET_IPV6_TRANSPARENT;
  CHECK_OPTION_SUPPORTED(expected_option);

  const int type = expected_option.value().first;
  const int option = expected_option.value().second;
  EXPECT_CALL(os_sys_calls_mock_, setsockopt_(_, _, _, _, sizeof(int)))
      .Times(2)
      .WillRepeatedly(Invoke([type, option](int, int input_type, int input_option,
                                            const void* optval, socklen_t) -> int {
        EXPECT_EQ(type, input_type);
        EXPECT_EQ(option, input_option);
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));

  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_PREBIND));
  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::api::v2::core::SocketOption::STATE_BOUND));
}

} // namespace
} // namespace Network
} // namespace Envoy
