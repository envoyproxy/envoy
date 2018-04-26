#include "test/common/network/socket_option_test.h"

namespace Envoy {
namespace Network {
namespace {

class SocketOptionImplTest : public SocketOptionTest {};

TEST_F(SocketOptionImplTest, BadFd) {
  EXPECT_EQ(-1, SocketOptionImpl::setSocketOption(socket_, {}, 0));
  EXPECT_EQ(ENOTSUP, errno);
}

TEST_F(SocketOptionImplTest, SetOptionSuccessTrue) {
  SocketOptionImpl socket_option{Socket::SocketState::PreBind,
                                 Network::SocketOptionName(std::make_pair(5, 10)), 1};
  EXPECT_CALL(os_sys_calls_, setsockopt_(_, 5, 10, _, sizeof(int)))
      .WillOnce(Invoke([](int, int, int, const void* optval, socklen_t) -> int {
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return 0;
      }));
  EXPECT_TRUE(socket_option.setOption(socket_, Socket::SocketState::PreBind));
}

} // namespace
} // namespace Network
} // namespace Envoy
