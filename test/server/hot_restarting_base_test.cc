#include "source/server/hot_restarting_base.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

using HotRestartMessage = envoy::HotRestartMessage;

class MockHotRestartingBase : public HotRestartingBase {
public:
  MockHotRestartingBase(const std::string& socket_path) : HotRestartingBase(0) {
    main_rpc_stream_.bindDomainSocket(1, "parent", socket_path, 0644);
  }
  bool sendMessage(sockaddr_un& address, const envoy::HotRestartMessage& message) {
    return main_rpc_stream_.sendHotRestartMessage(address, message);
  }
  std::unique_ptr<envoy::HotRestartMessage> receiveBlocking() {
    return main_rpc_stream_.receiveHotRestartMessage(RpcStream::Blocking::Yes);
  }
  bool socketIsNonBlocking() const {
    return (fcntl(main_rpc_stream_.domain_socket_, F_GETFL) & O_NONBLOCK) != 0;
  }
};

class HotRestartingBaseTest : public testing::Test {
public:
  HotRestartingBaseTest() : base_(path_) {}

  const std::string path_{"/tmp/source"};
  MockHotRestartingBase base_;
};

TEST_F(HotRestartingBaseTest, SendMsgRetryFailsAfterRetries) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillRepeatedly(Invoke(
          [&](os_fd_t, const msghdr*, int) { return Api::SysCallSizeResult{0, ECONNREFUSED}; }));

  std::string dst_path = "/tmp/dst";
  sockaddr_un sun;
  sun.sun_family = AF_UNIX;
  StringUtil::strlcpy(&sun.sun_path[1], dst_path.data(), dst_path.size());
  sun.sun_path[0] = '\0';

  HotRestartMessage message;
  message.mutable_request()->mutable_pass_listen_socket()->set_address("tcp://0.0.0.0:80");

  EXPECT_DEATH(base_.sendMessage(sun, message), "");
}

TEST_F(HotRestartingBaseTest, SendMsgRetrySucceedsEventually) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  bool retried = false;
  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _))
      .WillRepeatedly(Invoke([&](os_fd_t, const msghdr*, int) {
        if (!retried) {
          retried = true;
          return Api::SysCallSizeResult{0, ECONNREFUSED};
        }

        return Api::SysCallSizeResult{30, 0};
      }));

  std::string dst_path = "/tmp/dst";
  sockaddr_un sun;
  sun.sun_family = AF_UNIX;
  StringUtil::strlcpy(&sun.sun_path[1], dst_path.data(), dst_path.size());
  sun.sun_path[0] = '\0';

  HotRestartMessage message;
  message.mutable_request()->mutable_pass_listen_socket()->set_address("tcp://0.0.0.0:80");

  base_.sendMessage(sun, message);

  EXPECT_TRUE(retried);
}

// A blocking send that the socket's send timeout cuts short (the peer is not draining its
// socket) fails instead of asserting, and leaves the socket non-blocking.
TEST_F(HotRestartingBaseTest, SendMsgTimeoutFailsWithoutAsserting) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, sendmsg(_, _, _)).WillOnce(Invoke([&](os_fd_t, const msghdr*, int) {
    return Api::SysCallSizeResult{-1, EAGAIN};
  }));

  std::string dst_path = "/tmp/dst";
  sockaddr_un sun;
  sun.sun_family = AF_UNIX;
  StringUtil::strlcpy(&sun.sun_path[1], dst_path.data(), dst_path.size());
  sun.sun_path[0] = '\0';

  HotRestartMessage request;
  request.mutable_request()->mutable_stats();

  EXPECT_LOG_CONTAINS("error", "hot restart sendmsg() timed out",
                      EXPECT_FALSE(base_.sendMessage(sun, request)));
  EXPECT_TRUE(base_.socketIsNonBlocking());
}

// A blocking receive that the socket's receive timeout cuts short returns no message instead of
// asserting, and leaves the socket non-blocking for the caller to come back later.
TEST_F(HotRestartingBaseTest, BlockingReceiveReturnsNothingOnTimeout) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _)).WillOnce(Invoke([&](os_fd_t, msghdr*, int) {
    return Api::SysCallSizeResult{-1, EAGAIN};
  }));

  EXPECT_EQ(base_.receiveBlocking(), nullptr);
  EXPECT_TRUE(base_.socketIsNonBlocking());
}

} // namespace
} // namespace Server
} // namespace Envoy
