#pragma once

#include "envoy/config/core/v3/base.pb.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/network/address_impl.h"
#include "common/network/socket_option_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Network {
namespace {

class SocketOptionTest : public testing::Test {
public:
  SocketOptionTest() {
    socket_.local_address_.reset();

    EXPECT_CALL(os_sys_calls_, socket(_, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(
            Invoke([this](int domain, int type, int protocol) -> Api::SysCallSocketResult {
              return os_sys_calls_actual_.socket(domain, type, protocol);
            }));
    EXPECT_CALL(os_sys_calls_, setsocketblocking(_, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this](os_fd_t sockfd, bool block) -> Api::SysCallIntResult {
          return os_sys_calls_actual_.setsocketblocking(sockfd, block);
        }));
    EXPECT_CALL(os_sys_calls_, setsockopt_(_, IPPROTO_IPV6, IPV6_V6ONLY, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this](os_fd_t sockfd, int level, int optname, const void* optval,
                                      socklen_t optlen) -> int {
          return os_sys_calls_actual_.setsockopt(sockfd, level, optname, optval, optlen).rc_;
        }));
    EXPECT_CALL(os_sys_calls_, getsockopt_(_, _, _, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke(
            [this](os_fd_t sockfd, int level, int optname, void* optval, socklen_t* optlen) -> int {
              return os_sys_calls_actual_.getsockopt(sockfd, level, optname, optval, optlen).rc_;
            }));
    EXPECT_CALL(os_sys_calls_, getsockname(_, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke(
            [this](os_fd_t sockfd, sockaddr* name, socklen_t* namelen) -> Api::SysCallIntResult {
              return os_sys_calls_actual_.getsockname(sockfd, name, namelen);
            }));
  }

  NiceMock<MockListenSocket> socket_;
  Api::MockOsSysCalls os_sys_calls_;
  Api::OsSysCallsImpl os_sys_calls_actual_;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{[this]() {
    // Before injecting OsSysCallsImpl, make sure validateIpv{4,6}Supported is called so the static
    // bool is initialized without requiring to mock ::socket and ::close.
    std::make_unique<Address::Ipv4Instance>("1.2.3.4", 5678);
    std::make_unique<Address::Ipv6Instance>("::1:2:3:4", 5678);
    return &os_sys_calls_;
  }()};

  void testSetSocketOptionSuccess(
      Socket::Option& socket_option, Network::SocketOptionName option_name, int option_val,
      const std::set<envoy::config::core::v3::SocketOption::SocketState>& when) {
    for (auto state : when) {
      if (option_name.has_value()) {
        EXPECT_CALL(os_sys_calls_,
                    setsockopt_(_, option_name.level(), option_name.option(), _, sizeof(int)))
            .WillOnce(Invoke([option_val](os_fd_t, int, int, const void* optval, socklen_t) -> int {
              EXPECT_EQ(option_val, *static_cast<const int*>(optval));
              return 0;
            }));
        EXPECT_TRUE(socket_option.setOption(socket_, state));
      } else {
        EXPECT_FALSE(socket_option.setOption(socket_, state));
      }
    }

    // The set of SocketOption::SocketState for which this option should not be set.
    // Initialize to all the states, and remove states that are passed in.
    std::list<envoy::config::core::v3::SocketOption::SocketState> unset_socketstates{
        envoy::config::core::v3::SocketOption::STATE_PREBIND,
        envoy::config::core::v3::SocketOption::STATE_BOUND,
        envoy::config::core::v3::SocketOption::STATE_LISTENING,
    };
    unset_socketstates.remove_if(
        [&](envoy::config::core::v3::SocketOption::SocketState state) -> bool {
          return when.find(state) != when.end();
        });
    for (auto state : unset_socketstates) {
      EXPECT_CALL(os_sys_calls_, setsockopt_(_, _, _, _, _)).Times(0);
      EXPECT_TRUE(socket_option.setOption(socket_, state));
    }
  }

  Socket::Option::Details makeDetails(Network::SocketOptionName name, int value) {
    absl::string_view value_as_bstr(reinterpret_cast<const char*>(&value), sizeof(value));

    Socket::Option::Details expected_info;
    expected_info.name_ = name;
    expected_info.value_ = std::string(value_as_bstr);

    return expected_info;
  }
};

} // namespace
} // namespace Network
} // namespace Envoy
