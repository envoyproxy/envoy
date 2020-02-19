#include <sys/socket.h>

#include <memory>

#include "common/network/address_impl.h"

#include "extensions/quic_listeners/quiche/quic_io_handle_wrapper.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Quic {

class QuicIoHandleWrapperTest : public testing::Test {
public:
  QuicIoHandleWrapperTest() : wrapper_(std::make_unique<QuicIoHandleWrapper>(socket_.ioHandle())) {
    EXPECT_TRUE(wrapper_->isOpen());
    EXPECT_FALSE(socket_.ioHandle().isOpen());
  }
  ~QuicIoHandleWrapperTest() override = default;

protected:
  testing::NiceMock<Network::MockListenSocket> socket_;
  std::unique_ptr<QuicIoHandleWrapper> wrapper_;
  testing::StrictMock<Envoy::Api::MockOsSysCalls> os_sys_calls_;
  TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl> os_calls_{&os_sys_calls_};
};

TEST_F(QuicIoHandleWrapperTest, Close) {
  EXPECT_TRUE(wrapper_->close().ok());
  EXPECT_FALSE(wrapper_->isOpen());
}

TEST_F(QuicIoHandleWrapperTest, DelegateIoHandleCalls) {
  os_fd_t fd = socket_.ioHandle().fd();
  char data[5];
  Buffer::RawSlice slice{data, 5};
  EXPECT_CALL(os_sys_calls_, readv(fd, _, 1)).WillOnce(Return(Api::SysCallSizeResult{5u, 0}));
  wrapper_->readv(5, &slice, 1);

  EXPECT_CALL(os_sys_calls_, writev(fd, _, 1)).WillOnce(Return(Api::SysCallSizeResult{5u, 0}));
  wrapper_->writev(&slice, 1);

  EXPECT_CALL(os_sys_calls_, socket(AF_INET6, SOCK_STREAM, 0))
      .WillRepeatedly(Return(Api::SysCallSocketResult{1, 0}));
  EXPECT_CALL(os_sys_calls_, close(1)).WillRepeatedly(Return(Api::SysCallIntResult{0, 0}));

  Network::Address::InstanceConstSharedPtr addr(new Network::Address::Ipv4Instance(12345));
  EXPECT_CALL(os_sys_calls_, sendmsg(fd, _, 0)).WillOnce(Return(Api::SysCallSizeResult{5u, 0}));
  wrapper_->sendmsg(&slice, 1, 0, /*self_ip=*/nullptr, *addr);

  Network::IoHandle::RecvMsgOutput output(nullptr);
  EXPECT_CALL(os_sys_calls_, recvmsg(fd, _, 0)).WillOnce(Invoke([](os_fd_t, msghdr* msg, int) {
    sockaddr_storage ss;
    auto ipv6_addr = reinterpret_cast<sockaddr_in6*>(&ss);
    memset(ipv6_addr, 0, sizeof(sockaddr_in6));
    ipv6_addr->sin6_family = AF_INET6;
    ipv6_addr->sin6_addr = in6addr_loopback;
    ipv6_addr->sin6_port = htons(54321);
    *reinterpret_cast<sockaddr_in6*>(msg->msg_name) = *ipv6_addr;
    msg->msg_namelen = sizeof(sockaddr_in6);
    return Api::SysCallSizeResult{5u, 0};
  }));
  wrapper_->recvmsg(&slice, 1, /*self_port=*/12345, output);

  EXPECT_TRUE(wrapper_->close().ok());

  // Following calls shouldn't be delegated.
  wrapper_->readv(5, &slice, 1);
  wrapper_->writev(&slice, 1);
  wrapper_->sendmsg(&slice, 1, 0, /*self_ip=*/nullptr, *addr);
  wrapper_->recvmsg(&slice, 1, /*self_port=*/12345, output);
}

} // namespace Quic
} // namespace Envoy
