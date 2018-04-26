#include "common/network/address_impl.h"
#include "common/network/socket_option_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Network {
namespace {

class SocketOptionTest : public testing::Test {
public:
  SocketOptionTest() { socket_.local_address_.reset(); }

  NiceMock<MockListenSocket> socket_;
  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls{&os_sys_calls_};

  void testSetSocketOptionSuccess(SocketOptionImpl& socket_option, int socket_level,
                                  Network::SocketOptionName option_name, int option_val,
                                  const std::set<Socket::SocketState>& when) {
    Address::Ipv4Instance address("1.2.3.4", 5678);
    const int fd = address.socket(Address::SocketType::Stream);
    EXPECT_CALL(socket_, fd()).WillRepeatedly(Return(fd));

    for (Socket::SocketState state : when) {
      if (option_name.has_value()) {
        EXPECT_CALL(os_sys_calls_,
                    setsockopt_(_, socket_level, option_name.value(), _, sizeof(int)))
            .WillOnce(Invoke([option_val](int, int, int, const void* optval, socklen_t) -> int {
              EXPECT_EQ(option_val, *static_cast<const int*>(optval));
              return 0;
            }));
        EXPECT_TRUE(socket_option.setOption(socket_, state));
      } else {
        EXPECT_FALSE(socket_option.setOption(socket_, state));
      }
    }

    // The set of SocketState for which this option should not be set. Initialize to all
    // the states, and remove states that are passed in.
    std::list<Socket::SocketState> unset_socketstates{
        Socket::SocketState::PreBind,
        Socket::SocketState::PostBind,
        Socket::SocketState::Listening,
    };
    unset_socketstates.remove_if(
        [&](Socket::SocketState state) -> bool { return when.find(state) != when.end(); });
    for (Socket::SocketState state : unset_socketstates) {
      EXPECT_CALL(os_sys_calls_, setsockopt_(_, _, _, _, _)).Times(0);
      EXPECT_TRUE(socket_option.setOption(socket_, state));
    }
  }
};

} // namespace
} // namespace Network
} // namespace Envoy
